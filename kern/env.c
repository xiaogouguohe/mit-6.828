/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/monitor.h>
#include <kern/sched.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>

struct Env *envs = NULL;		// All environments
static struct Env *env_free_list;	// Free environment list
					// (linked by Env->env_link)

#define ENVGENSHIFT	12		// >= LOGNENV

// Global descriptor table.

// Set up global descriptor table (GDT) with separate segments for
// kernel mode and user mode.  Segments serve many purposes on the x86.
// We don't use any of their memory-mapping capabilities, but we need
// them to switch privilege levels. 

// The kernel and user segments are identical except for the DPL.
// To load the SS register, the CPL must equal the DPL.  Thus,
// we must duplicate the segments for the user and the kernel.

// In particular, the last argument to the SEG macro used in the
// definition of gdt specifies the Descriptor Privilege Level (DPL)
// of that descriptor: 0 for kernel and 3 for user.
//
struct Segdesc gdt[NCPU + 5] =
{
	// 0x0 - unused (always faults -- for trapping NULL far pointers)
	SEG_NULL,

	// 0x8 - kernel code segment
	[GD_KT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 0),

	// 0x10 - kernel data segment
	[GD_KD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 0),

	// 0x18 - user code segment
	[GD_UT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 3),

	// 0x20 - user data segment
	[GD_UD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 3),

	// Per-CPU TSS descriptors (starting from GD_TSS0) are initialized
	// in trap_init_percpu()
	[GD_TSS0 >> 3] = SEG_NULL
};

/* gdt的大小和地址，
在env_init_percpu当中会通过lgdt，存放到gdtr控制寄存器当中
深入理解Linux内核2.2.2 */
struct Pseudodesc gdt_pd = {
	sizeof(gdt) - 1, (unsigned long) gdt
};

//
// Converts an envid to an env pointer.
// If checkperm is set, the specified environment must be either the
// current environment or an immediate child of the current environment.
//
// RETURNS
//   0 on success, -E_BAD_ENV on error.
//   On success, sets *env_store to the environment.
//   On error, sets *env_store to NULL.
//
int
envid2env(envid_t envid, struct Env **env_store, bool checkperm)
{
	struct Env *e;

	// If envid is zero, return the current environment.
	if (envid == 0) {
		*env_store = curenv;
		return 0;
	}

	// Look up the Env structure via the index part of the envid,
	// then check the env_id field in that struct Env
	// to ensure that the envid is not stale
	// (i.e., does not refer to a _previous_ environment
	// that used the same slot in the envs[] array).
	e = &envs[ENVX(envid)];
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment
	// must be either the current environment
	// or an immediate child of the current environment.
	if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	*env_store = e;
	return 0;
}

// Mark all environments in 'envs' as free, set their env_ids to 0,
// and insert them into the env_free_list.
// Make sure the environments are in the free list in the same order
// they are in the envs array (i.e., so that the first call to
// env_alloc() returns envs[0]).
//
/* 初始化envs数组，全部空闲，env_id全设为0，空闲链表头为envs[0] */
void
env_init(void)
{
	// Set up envs array
	// LAB 3: Your code here.
	env_free_list = &envs[0];

	for (uint32_t i = 0; i < NENV; i++) {
		envs[i].env_id = 0;
		envs[i].env_link = (i == NENV - 1) ? NULL : &envs[i + 1];
	}

	// Per-CPU part of the initialization
	env_init_percpu();
}

// Load GDT and segment descriptors.
void
env_init_percpu(void)
{
	/* 把gdt的地址和大小加载到gdtr */
	lgdt(&gdt_pd);
	// The kernel never uses GS or FS, so we leave those set to
	// the user data segment.
	/* 段寄存器存放段选择符，参考深入理解Linux内核2.2.1，
	还有一些相关概念，参考深入理解Linux内核2.2.5 */
	/* 段寄存器gs和fs存放段选择符GD_UD，也就是用户数据段的段选择符，
	段选择符的后两位是RPL，表示请求的特权级，设为3表示请求是用户级 */
	asm volatile("movw %%ax,%%gs" : : "a" (GD_UD|3));
	asm volatile("movw %%ax,%%fs" : : "a" (GD_UD|3));
	// The kernel does use ES, DS, and SS.  We'll change between
	// the kernel and user data segments as needed.
	/* 段寄存器es, ds和ss存放段选择符GD_KD，也就是内核数据段的段选择符，
	请求内核数据段的请求级别为内核级 */
	asm volatile("movw %%ax,%%es" : : "a" (GD_KD));
	asm volatile("movw %%ax,%%ds" : : "a" (GD_KD));
	asm volatile("movw %%ax,%%ss" : : "a" (GD_KD));
	// Load the kernel text segment into CS.
	/* 段寄存器cs存放段选择符GD_KT，也就是内核代码段的段选择符 */
	asm volatile("ljmp %0,$1f\n 1:\n" : : "i" (GD_KT));
	// For good measure, clear the local descriptor table (LDT),
	// since we don't use it.
	lldt(0);
}

//
// Initialize the kernel virtual memory layout for environment e.
// Allocate a page directory, set e->env_pgdir accordingly,
// and initialize the kernel portion of the new environment's address space.
// Do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// Returns 0 on success, < 0 on error.  Errors include:
//	-E_NO_MEM if page directory or table could not be allocated.
//
/* 为环境e初始化虚拟地址空间，
分配页表，
初始化虚拟地址空间的内核部分，
不要对虚拟地址空间的用户部分做任何映射 */
static int
env_setup_vm(struct Env *e)
{
	int i;
	struct PageInfo *p = NULL;

	// Allocate a page for the page directory
	if (!(p = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;

	// Now, set e->env_pgdir and initialize the page directory.
	//
	// Hint:
	//    - The VA space of all envs is identical above UTOP
	//	(except at UVPT, which we've set below).
	//	See inc/memlayout.h for permissions and layout.
	//	Can you use kern_pgdir as a template?  Hint: Yes.
	//	(Make sure you got the permissions right in Lab 2.)
	//    - The initial VA below UTOP is empty.
	//    - You do not need to make any more calls to page_alloc.
	//    - Note: In general, pp_ref is not maintained for
	//	physical pages mapped only above UTOP, but env_pgdir
	//	is an exception -- you need to increment env_pgdir's
	//	pp_ref for env_free to work correctly.
	//    - The functions in kern/pmap.h are handy.
	/* 设置e->env_pgdir，并且初始化页表。
	所有环境在UTOP之上的虚拟地址空间都是一样的，除了UVPT以外。
	kern_pgdir可以被用做模板？？？
	- 初始化后的虚拟地址空间的UTOP之下的部分是空的，
	- 
	- 一般来说，pp_ref是不会给映射到UTOP之上的物理页进行计数的，
	但env_pgdir是个例外，这样才能使env_free正常工作，
	kern/pmap.h是可用的 */

	// LAB 3: Your code here.
	// /* 和kern_pgdir的分配和初始化类似 */
	// region_alloc(e, e->env_pgdir, PGSIZE);
	// memset(e->env_pgdir, 0, PGSIZE);
	
	// /* e->env_pgdir的物理页的引用计数要+1，为什么？？？ */
	// struct PageInfo* pp = pa2page(PADDR(e->env_pgdir));
	// pp->pp_ref++;

	/* 用户环境的页目录虚拟地址和刚才得到的物理页建立映射 */
	e->env_pgdir = page2kva(p);
	/* 该物理页的引用计数+1，为什么？？？ */
	p->pp_ref++;

	/* 所有环境在UTOP之上的虚拟地址空间一样 */
	memcpy(e->env_pgdir, kern_pgdir, PGSIZE);

	/* UTOP之下的虚拟地址被清零初始化 */
	for (uint32_t i = 0; i < PDX(UTOP); i++) {
		e->env_pgdir[i] = 0;
	}

	// UVPT maps the env's own page table read-only.
	// Permissions: kernel R, user R
	/* 和kern_pgdir类似，e->env_pgdir的物理地址和UVPT建立映射 */
	e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;

	return 0;
}

//
// Allocates and initializes a new environment.
// On success, the new environment is stored in *newenv_store.
//
// Returns 0 on success, < 0 on failure.  Errors include:
//	-E_NO_FREE_ENV if all NENV environments are allocated
//	-E_NO_MEM on memory exhaustion
//
/* 分配一个环境到newenv_store，父环境为parent_id */
int
env_alloc(struct Env **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	struct Env *e;

	/* 没有多余的环境 */
	if (!(e = env_free_list))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	/* 为环境建立虚拟地址空间失败 */
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	/* 为新环境分配环境id */
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);

	// Set the basic status variables.
	/* 初始化环境 */
	/* 父环境id */
	e->env_parent_id = parent_id;
	/* 用户环境 */
	e->env_type = ENV_TYPE_USER;
	/* 环境状态为运行态 */
	e->env_status = ENV_RUNNABLE;
	/* 环境当前运行次数为0 */
	e->env_runs = 0;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	/* 初始化环境相关的寄存器 */
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.  When
	// we switch privilege levels, the hardware does various
	// checks involving the RPL and the Descriptor Privilege Level
	// (DPL) stored in the descriptors themselves.
	/* 参考env_init_percpu的注释 */
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_eip later.

	// Enable interrupts while in user mode.
	// LAB 4: Your code here.

	// Clear the page fault handler until user installs one.
	e->env_pgfault_upcall = 0;

	// Also clear the IPC receiving flag.
	e->env_ipc_recving = 0;

	// commit the allocation
	/* 从空闲链表中取出新环境，并且把新环境e存到参数 */
	env_free_list = e->env_link;
	*newenv_store = e;

	cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	return 0;
}

//
// Allocate len bytes of physical memory for environment env,
// and map it at virtual address va in the environment's address space.
// Does not zero or otherwise initialize the mapped pages in any way.
// Pages should be writable by user and kernel.
// Panic if any allocation attempt fails.
//
/* 给env分配len字节的物理内存，
把这段物理内存映射到env的虚拟地址va，
不要初始化要映射的页，
内核和用户都可读写页，
如果分配失败，panic */
static void
region_alloc(struct Env *e, void *va, size_t len)
{
	// LAB 3: Your code here.
	// (But only if you need it for load_icode.)
	//
	// Hint: It is easier to use region_alloc if the caller can pass
	//   'va' and 'len' values that are not page-aligned.
	//   You should round va down, and round (va + len) up.
	//   (Watch out for corner-cases!)
	uint32_t down = ROUNDDOWN((uintptr_t) va, PGSIZE);
	uint32_t up = ROUNDUP((uintptr_t)(va) + len, PGSIZE);
	for (; down < up; down += PGSIZE) {
		// 不要初始化，为什么？？？
		struct PageInfo *pp = NULL;
		if (!(pp = page_alloc(0))) {
			panic("in func region_alloc, alloc page fail!");
		}	
		if (page_insert(e->env_pgdir, pp, (void*)down, PTE_U | PTE_W) != 0) {
			panic("in func region_alloc, map page fail!");
		}
	}
}

// Set up the initial program binary, stack, and processor flags
// for a user process.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.

// This function loads all loadable segments from the ELF binary image
// into the environment's user memory, starting at the appropriate
// virtual addresses indicated in the ELF program header.
// At the same time it clears to zero any portions of these segments
// that are marked in the program header as being mapped
// but not actually present in the ELF file - i.e., the program's bss section.

// All this is very similar to what our boot loader does, except the boot
// loader also needs to read the code from disk.  Take a look at
// boot/main.c to get ideas.

// Finally, this function maps one page for the program's initial stack.

// load_icode panics if it encounters problems.
//  - How might load_icode fail?  What might be wrong with the given input?
/* 为用户进程设置初始二进制代码区，堆栈，还有处理器标志位，
此函数仅在内核初始化期间，也就是在运行第一个用户态环境之前被调用

函数只在内核初始化时，也就是第一个用户环境运行之前被调用

此函数把段从ELF二进制映像加载到环境的用户态内存，
加载地址从合适的虚拟地址开始，这个虚拟地址是由ELF头指示的

同时，需要把段的这些部分清零，这些部分在program header被标记为已映射但实际并未出现在ELF文件，
这些段说的就是bss段 
 */

static void
load_icode(struct Env *e, uint8_t *binary)
{
	// Hints:
	//  Load each program segment into virtual memory
	//  at the address specified in the ELF segment header.
	//  You should only load segments with ph->p_type == ELF_PROG_LOAD.
	//  Each segment's virtual address can be found in ph->p_va
	//  and its size in memory can be found in ph->p_memsz.
	//  The ph->p_filesz bytes from the ELF binary, starting at
	//  'binary + ph->p_offset', should be copied to virtual address
	//  ph->p_va.  Any remaining memory bytes should be cleared to zero.
	//  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
	//  Use functions from the previous lab to allocate and map pages.
	//
	//  All page protection bits should be user read/write for now.
	//  ELF segments are not necessarily page-aligned, but you can
	//  assume for this function that no two segments will touch
	//  the same virtual page.
	//
	//  You may find a function like region_alloc useful.
	//
	//  Loading the segments is much simpler if you can move data
	//  directly into the virtual addresses stored in the ELF binary.
	//  So which page directory should be in force during
	//  this function?
	//
	//  You must also do something with the program's entry point,
	//  to make sure that the environment starts executing there.
	//  What?  (See env_run() and env_pop_tf() below.)
	/* 把程序的每个段加载到虚拟地址，
	这个虚拟地址有elf文件的段头（段头部表）指定，参考caspp7.8，图7-13
	
	所有页的权限位都应当设为用户可读写
	
	ELF段不需要页对齐，但是在这里可以假设不会出现两个段占用同样的虚拟页
	
	程序入口需要做一些处理，来保证环境从这个点开始执行，
	参考下文的 env_run() 和 env_pop_tf() */

	// LAB 3: Your code here.
	/* binary是环境e的ELF可执行文件的ELF头 */
	struct Elf* elf_header = (struct Elf*) binary;
	/* 判断该ELF文件是否合法 */
	if (elf_header->e_magic != ELF_MAGIC) {
		return;
	}

	e->env_tf.tf_eip = elf_header->e_entry;

	/* 现在要把段加载到用户环境e的虚拟地址空间，
	因此cr3应该存放用户环境e的页目录物理地址，否则memcpy等函数会出问题，
	因为通过用户环境e的虚拟地址ph->p_va访问，要经过e的页表转换成物理地址，
	其它操作都是内核环境做的事
	是这个原因吗？？？lcr3意味着什么？？？ */
	lcr3(PADDR(e->env_pgdir));

	/* e_phoff 是Program Header Table对于ELF头的偏移量，
	ph是Program Header Table的起始地址，
	eph是Program Header Table的末尾地址，
	每个Program Header Table的表项都保存了一个段的信息 */
	// struct Proghdr *ph, *eph;
    // ph = (struct Proghdr* )((uint8_t *)header + header->e_phoff);
    // eph = ph + header->e_phnum;
	struct Proghdr* ph = (struct Proghdr *) ((uint8_t *)elf_header + elf_header->e_phoff);
	struct Proghdr* eph = ph + elf_header->e_phnum;
	for (; ph < eph; ph++) {
		/* 只加载符合这个条件的段 */
		if (ph->p_type == ELF_PROG_LOAD) {
			/* 要满足段的大小小于内存中留给这个段的空间 */
			if (ph->p_memsz < ph->p_filesz) {
				panic("in func load_icode, failed becasue p_memsz < p_filesz!s");
			}

			/* 需要先给虚拟地址ph->p_va分配物理内存，并建立映射关系，
			才能把虚拟地址binary + ph->p_offset的内容复制到ph->p_va */
			region_alloc(e, (void*)ph->p_va, ph->p_memsz);

			/*ELF二进制文件的从"binary + ph->p_offset"开始的ph->p_filesz字节，需要被复制到虚拟地址ph->p_va*/
			memcpy((void*)(ph->p_va), (void*)(binary + ph->p_offset), ph->p_filesz); 
			/* 剩余内存的字节应当被清零 */
			memset((void*)ph->p_va+ ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
		}
	}

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.

	// LAB 3: Your code here.
	// 需要吗？？？
	/* 切换回内核的页目录 */
	// lcr3(PADDR(kern_pgdir));

	// 不能用上面的，这是内核的页操作
	// struct PageInfo* pp = page_alloc(1);
	// if (!pp) {
	// 	panic("in func load_icode, fail because can not alloc page!");
	// }
	// if (page_insert(e->env_pgdir, pp, USTACKTOP - PGSIZE, PTE_U | PTE_W) != 0 ) {
	// 	panic("in func load_icode, fail becasue can not map page for initial stack!");
	// }
	region_alloc(e, (void*)USTACKTOP - PGSIZE, PGSIZE);
}

//
// Allocates a new env with env_alloc, loads the named elf
// binary into it with load_icode, and sets its env_type.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
// The new env's parent ID is set to 0.
//
void
env_create(uint8_t *binary, enum EnvType type)
{
	// LAB 3: Your code here.
	struct Env* env = NULL;
	env_alloc(&env, 0);

	load_icode(env, binary);

	env->env_type = type;
	// cprintf("in func env_create, envs: %x, env: %x\n", envs, env);
	// panic("in func env_create, 111");
}

//
// Frees env e and all memory it uses.
//
void
env_free(struct Env *e)
{
	pte_t *pt;
	uint32_t pdeno, pteno;
	physaddr_t pa;

	// If freeing the current environment, switch to kern_pgdir
	// before freeing the page directory, just in case the page
	// gets reused.
	if (e == curenv)
		lcr3(PADDR(kern_pgdir));

	// Note the environment's demise.
	cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

	// Flush all mapped pages in the user portion of the address space
	static_assert(UTOP % PTSIZE == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*) KADDR(pa);

		// unmap all PTEs in this page table
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
				page_remove(e->env_pgdir, PGADDR(pdeno, pteno, 0));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	// free the page directory
	pa = PADDR(e->env_pgdir);
	e->env_pgdir = 0;
	page_decref(pa2page(pa));

	// return the environment to the free list
	e->env_status = ENV_FREE;
	e->env_link = env_free_list;
	env_free_list = e;
}

//
// Frees environment e.
// If e was the current env, then runs a new environment (and does not return
// to the caller).
//
void
env_destroy(struct Env *e)
{
	// If e is currently running on other CPUs, we change its state to
	// ENV_DYING. A zombie environment will be freed the next time
	// it traps to the kernel.
	if (e->env_status == ENV_RUNNING && curenv != e) {
		e->env_status = ENV_DYING;
		return;
	}

	env_free(e);

	if (curenv == e) {
		curenv = NULL;
		sched_yield();
	}
}


//
// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
//
// This function does not return.
//
void
env_pop_tf(struct Trapframe *tf)
{
	// Record the CPU we are running on for user-space debugging
	curenv->env_cpunum = cpunum();

	asm volatile(
		/* 栈顶指针指向tf起始地址，
		回忆函数调用时用户栈的变化，此时栈顶是结构体类型Trapframe的实例tf，
		而tf的开头是通用寄存器的结构体类型PushRegs的实例，
		注意下面代码出栈的顺序，和Trapframe成员的顺序是完全一致的 */
		"\tmovl %0,%%esp\n" 
		/* 弹出PushRegs中保存的所有通用寄存器的值 */
		"\tpopal\n"
		/* 弹出寄存器%es, %ds的值，保存到寄存器%es, %ds */
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		/* 跳过成员tf_trapno和tf_errcode */
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		/* 中断返回指令，返回到被中断的程序，也就是调用这个函数的地方
		
		从Trapframe结构中依次弹出tf_eip, tf_cs, tf_eflags, tf_esp, tf_ss到相应寄存器 */
		"\tiret\n"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//
// This function does not return.
//
/* 切换环境 */
void
env_run(struct Env *e)
{
	// Step 1: If this is a context switch (a new environment is running):
	//	   1. Set the current environment (if any) back to
	//	      ENV_RUNNABLE if it is ENV_RUNNING (think about
	//	      what other states it can be in),
	//	   2. Set 'curenv' to the new environment,
	//	   3. Set its status to ENV_RUNNING,
	//	   4. Update its 'env_runs' counter,
	//	   5. Use lcr3() to switch to its address space.
	// Step 2: Use env_pop_tf() to restore the environment's
	//	   registers and drop into user mode in the
	//	   environment.

	// Hint: This function loads the new environment's state from
	//	e->env_tf.  Go back through the code you wrote above
	//	and make sure you have set the relevant parts of
	//	e->env_tf to sensible values.

	// LAB 3: Your code here.
	if (curenv) {
		/* 更改之前运行的环境的状态为就绪 */
		if (curenv->env_status == ENV_RUNNING) {
			curenv->env_status = ENV_RUNNABLE;
		}
	}

	curenv = e;

	e->env_status = ENV_RUNNING;

	e->env_runs++;

	/* 把新环境e的页目录物理地址存到cr3寄存器 */
	lcr3(PADDR(e->env_pgdir));

	/* 把新环境的上次运行时保存的寄存器的值，放到相应的寄存器中 */
	// panic("in func env_run, 111\n");
	env_pop_tf(&e->env_tf);

	/* 正常情况下不会执行 */
	panic("env_run not yet implemented");
}

