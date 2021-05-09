#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>

static struct Taskstate ts;

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < ARRAY_SIZE(excnames))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}


void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.
	/* 这里是函数声明，不是函数调用 */
	void th0();
	void th1();
	void th3();
	void th4();
	void th5();
	void th6();
	void th7();
	void th8();
	void th9();
	void th10();
	void th11();
	void th12();
	void th13();
	void th14();
	void th16();
	void th_syscall();

	/* 设置IDT表项，
	gate参数为idt[0]，表明了要设置表项idt[0]；
	istrap参数为0，说明为异常；
	sel为GD_KT，说明中断处理函数所在段为内核代码段；
	off为th0，说明中断处理函数的段内偏移量为th0，
	因为程序里的指针，就是段内偏移；
	dpl为0，说明段的特权级为内核级
	 */
	SETGATE(idt[0], 0, GD_KT, th0, 0);		
	SETGATE(idt[1], 0, GD_KT, th1, 0);  
	SETGATE(idt[3], 0, GD_KT, th3, 3);
	SETGATE(idt[4], 0, GD_KT, th4, 0);
	SETGATE(idt[5], 0, GD_KT, th5, 0);
	SETGATE(idt[6], 0, GD_KT, th6, 0);
	SETGATE(idt[7], 0, GD_KT, th7, 0);
	SETGATE(idt[8], 0, GD_KT, th8, 0);
	SETGATE(idt[9], 0, GD_KT, th9, 0);
	SETGATE(idt[10], 0, GD_KT, th10, 0);
	SETGATE(idt[11], 0, GD_KT, th11, 0);
	SETGATE(idt[12], 0, GD_KT, th12, 0);
	SETGATE(idt[13], 0, GD_KT, th13, 0);
	SETGATE(idt[14], 0, GD_KT, th14, 0);
	SETGATE(idt[16], 0, GD_KT, th16, 0);

	SETGATE(idt[T_SYSCALL], 0, GD_KT, th_syscall, 3);

	// Per-CPU setup 
	trap_init_percpu();
}

// Initialize and load the per-CPU TSS and IDT
void
trap_init_percpu(void)
{
	// The example code here sets up the Task State Segment (TSS) and
	// the TSS descriptor for CPU 0. But it is incorrect if we are
	// running on other CPUs because each CPU has its own kernel stack.
	// Fix the code so that it works for all CPUs.
	//
	// Hints:
	//   - The macro "thiscpu" always refers to the current CPU's
	//     struct CpuInfo;
	//   - The ID of the current CPU is given by cpunum() or
	//     thiscpu->cpu_id;
	//   - Use "thiscpu->cpu_ts" as the TSS for the current CPU,
	//     rather than the global "ts" variable;
	//   - Use gdt[(GD_TSS0 >> 3) + i] for CPU i's TSS descriptor;
	//   - You mapped the per-CPU kernel stacks in mem_init_mp()
	//   - Initialize cpu_ts.ts_iomb to prevent unauthorized environments
	//     from doing IO (0 is not the correct value!)
	//
	// ltr sets a 'busy' flag in the TSS selector, so if you
	// accidentally load the same TSS on more than one CPU, you'll
	// get a triple fault.  If you set up an individual CPU's TSS
	// wrong, you may not get a fault until you try to return from
	// user space on that CPU.
	//
	// LAB 4: Your code here:
	// /* 这是发生在一块 CPU 上的，所以不需要遍历每块 CPU */
	// // for (uint32_t i = 0; i < NCPU; i++) {
	// // 	thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - KSTKSIZE - i * (KSTKSIZE + KSTKGAP);
	// // 	thiscpu->cpu_ts.ts_ss0 = 0;
	// // }

	// // Setup a TSS so that we get the right stack
	// // when we trap to the kernel.
	
	// thiscpu->cpu_ts.ts_esp0 = KSTACKTOP;
	// thiscpu->cpu_ts.ts_ss0 = GD_KD;
	// thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

	// // Initialize the TSS slot of the gdt.
	// gdt[GD_TSS0 >> 3] = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
	// 				sizeof(struct Taskstate) - 1, 0);
	// gdt[GD_TSS0 >> 3].sd_s = 0;

	// // Load the TSS selector (like other segment selectors, the
	// // bottom three bits are special; we leave them 0)
	// ltr(GD_TSS0);

	// // Load the IDT
	// /* idt寄存器存放idt_pd，idt_pd记录了IDT的地址和大小 */
	// lidt(&idt_pd);
    
    int cid = thiscpu->cpu_id;
	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - cid * (KSTKSIZE + KSTKGAP);
	thiscpu->cpu_ts.ts_ss0 = GD_KD;

	// Initialize the TSS slot of the gdt.
	gdt[(GD_TSS0 >> 3)+cid] = SEG16(STS_T32A, (uint32_t) (&(thiscpu->cpu_ts)),
					sizeof(struct Taskstate), 0);
	gdt[(GD_TSS0 >> 3)+cid].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSS0+8*cid);

	// Load the IDT
	lidt(&idt_pd);
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p from CPU %d\n", tf, cpunum());
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	if ((tf->tf_cs & 3) != 0) {
		cprintf("  esp  0x%08x\n", tf->tf_esp);
		cprintf("  ss   0x----%04x\n", tf->tf_ss);
	}
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	// // Handle processor exceptions.
	// // LAB 3: Your code here.

	// // Handle spurious interrupts
	// // The hardware sometimes raises these because of noise on the
	// // IRQ line or other reasons. We don't care.
	// if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
	// 	cprintf("Spurious interrupt on irq 7\n");
	// 	print_trapframe(tf);
	// 	return;
	// }

	// // Handle clock interrupts. Don't forget to acknowledge the
	// // interrupt using lapic_eoi() before calling the scheduler!
	// // LAB 4: Your code here.

	// // Unexpected trap: The user process or the kernel has a bug.
	// print_trapframe(tf);
	// if (tf->tf_cs == GD_KT)
	// 	panic("unhandled trap in kernel");
	// else {
	// 	env_destroy(curenv);
	// 	return;
	// }

	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	}

	int32_t retcode = 0;
	switch (tf->tf_trapno)
	{
	case T_PGFLT:
		page_fault_handler(tf);
		break;
	case T_BRKPT:
		/* 如果是用户手动设置的断点导致的中断，就输出寄存器信息 */
		monitor(tf);
		break;
	case T_SYSCALL:
		/* 如果是系统调用导致的中断，就调用系统调用kern/syscall.c的syacall函数，
		参数是这些寄存器的值，
		返回值放到eax寄存器 */
		// cprintf("in func trap_dispatch, case is T_SYSCALL\n");
		retcode = syscall(
			tf->tf_regs.reg_eax,
			tf->tf_regs.reg_edx,
			tf->tf_regs.reg_ecx,
			tf->tf_regs.reg_ebx,
			tf->tf_regs.reg_edi,
			tf->tf_regs.reg_esi);
		tf->tf_regs.reg_eax = retcode;
		break;
	
	
	default:
		// Unexpected trap: The user process or the kernel has a bug.
		print_trapframe(tf);
		if (tf->tf_cs == GD_KT)
			panic("unhandled trap in kernel");
		else {
			env_destroy(curenv);
			return;
		}
	}

}

void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Halt the CPU if some other CPU has called panic()
	extern char *panicstr;
	if (panicstr)
		asm volatile("hlt");

	// Re-acqurie the big kernel lock if we were halted in
	// sched_yield()
	if (xchg(&thiscpu->cpu_status, CPU_STARTED) == CPU_HALTED)
		lock_kernel();
	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	/* 判断中断机制是否打开 */
	assert(!(read_eflags() & FL_IF));

	/* 被中断的程序是用户态，
	如果是，需要把用户态的寄存器的值保存在当前环境的env_tf成员，
	才能在下次在用户态运行这个环境的时候，恢复寄存器
	其实寄存器的值已经保留在了内核栈，不知道是否一定要再保存一份在env_tf成员？？？
	什么时候写进内核栈的？？？ */
	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Acquire the big kernel lock before doing any
		// serious kernel work.
		/* 之前是用户态，现在切换到内核态，需要获取内核锁 */

		// LAB 4: Your code here.
		lock_kernel();
		assert(curenv);

		// Garbage collect if current enviroment is a zombie
		if (curenv->env_status == ENV_DYING) {
			env_free(curenv);
			curenv = NULL;
			sched_yield();
		}

		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		/* 把被中断的程序的寄存器状态保存到当前环境curenv的成员env_tf，
		之所以要从内核栈复制一份，可能是因为不一定能从内核栈取出来？？？ */
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
}


void
page_fault_handler(struct Trapframe *tf)
{
	// uint32_t fault_va;

	// // Read processor's CR2 register to find the faulting address
	// fault_va = rcr2();

	// // Handle kernel-mode page faults.

	// // LAB 3: Your code here.
	// if ((tf->tf_cs & 3) == 0) {
	// 	panic("in func page_fault_handler, page fault happens in kernel mode!");
	// }

	// // We've already handled kernel-mode exceptions, so if we get here,
	// // the page fault happened in user mode.

	// // Call the environment's page fault upcall, if one exists.  Set up a
	// // page fault stack frame on the user exception stack (below
	// // UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	// //
	// // The page fault upcall might cause another page fault, in which case
	// // we branch to the page fault upcall recursively, pushing another
	// // page fault stack frame on top of the user exception stack.
	// //
	// // It is convenient for our code which returns from a page fault
	// // (lib/pfentry.S) to have one word of scratch space at the top of the
	// // trap-time stack; it allows us to more easily restore the eip/esp. In
	// // the non-recursive case, we don't have to worry about this because
	// // the top of the regular user stack is free.  In the recursive case,
	// // this means we have to leave an extra word between the current top of
	// // the exception stack and the new stack frame because the exception
	// // stack _is_ the trap-time stack.
	// //
	// // If there's no page fault upcall, the environment didn't allocate a
	// // page for its exception stack or can't write to it, or the exception
	// // stack overflows, then destroy the environment that caused the fault.
	// // Note that the grade script assumes you will first check for the page
	// // fault upcall and print the "user fault va" message below if there is
	// // none.  The remaining three checks can be combined into a single test.
	// //
	// // Hints:
	// //   user_mem_assert() and env_run() are useful here.
	// //   To change what the user environment runs, modify 'curenv->env_tf'
	// //   (the 'tf' variable points at 'curenv->env_tf').

	// // LAB 4: Your code here.
    // /* 用户态错误栈的栈帧信息 */
    // struct UTrapframe* utf;

    // /* 有注册用户态页异常的处理函数 */
    // if (curenv->env_pgfault_upcall) {
    //     /* tf 是发生异常时的内核栈帧信息，或者说各寄存器的值
    //     判断栈顶指针是否在用户态错误栈的范围内 */
    //     if (UXSTACKTOP - PGSIZE <=  tf->tf_esp && tf->tf_esp <= UXSTACKTOP) {
    //         /* 当前栈顶指针在用户态错误栈的范围内，也就是说是递归异常，
    //         用户异常之间需要留出 4 字节的空间，猜测是为了防止越界 */
    //         utf = (struct UTrapframe*) (tf->tf_esp - sizeof(struct UTrapframe) - 4);
    //     } else {
    //         /* 当前栈顶指针不在用户态错误栈范围内，也就是在内核态错误栈内，
    //         直接跳转到用户态错误栈 */
    //         utf = (struct UTrapframe*) (UXSTACKTOP - sizeof(struct UTrapframe));
    //     }

    //     /* 检查当前进程是否对这块空间有写权限 */
    //     user_mem_assert(curenv, utf, sizeof(struct UTrapframe), PTE_W);
    //     utf->utf_fault_va = fault_va;
    //     utf->utf_err = tf->tf_trapno;
    //     utf->utf_eip = tf->tf_eip;
    //     utf->utf_eflags = tf->tf_eflags;
    //     utf->utf_esp = tf->tf_esp;
    //     utf->utf_regs = tf->tf_regs;
    //     tf->tf_eip = (uint32_t)curenv->env_pgfault_upcall;
    //     tf->tf_esp = (uint32_t)utf;
    //     env_run(curenv);

    // }

	// // Destroy the environment that caused the fault.
	// cprintf("[%08x] user fault va %08x ip %08x\n",
	// 	curenv->env_id, fault_va, tf->tf_eip);
	// print_trapframe(tf);
	// env_destroy(curenv);

    uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// LAB 3: Your code here.
	if ((tf->tf_cs & 3) == 0)
		panic("page_fault_handler():page fault in kernel mode!\n");

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// It is convenient for our code which returns from a page fault
	// (lib/pfentry.S) to have one word of scratch space at the top of the
	// trap-time stack; it allows us to more easily restore the eip/esp. In
	// the non-recursive case, we don't have to worry about this because
	// the top of the regular user stack is free.  In the recursive case,
	// this means we have to leave an extra word between the current top of
	// the exception stack and the new stack frame because the exception
	// stack _is_ the trap-time stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// Note that the grade script assumes you will first check for the page
	// fault upcall and print the "user fault va" message below if there is
	// none.  The remaining three checks can be combined into a single test.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	// LAB 4: Your code here.
	if (curenv->env_pgfault_upcall) {
		uintptr_t stacktop = UXSTACKTOP;
		if (UXSTACKTOP - PGSIZE < tf->tf_esp && tf->tf_esp < UXSTACKTOP) {
			stacktop = tf->tf_esp;
		}
		uint32_t size = sizeof(struct UTrapframe) + sizeof(uint32_t);
		user_mem_assert(curenv, (void *)stacktop - size, size, PTE_U | PTE_W);
		struct UTrapframe *utr = (struct UTrapframe *)(stacktop - size);
		utr->utf_fault_va = fault_va;
		utr->utf_err = tf->tf_err;
		utr->utf_regs = tf->tf_regs;
		utr->utf_eip = tf->tf_eip;
		utr->utf_eflags = tf->tf_eflags;
		utr->utf_esp = tf->tf_esp;				//UXSTACKTOP栈上需要保存发生缺页异常时的%esp和%eip

		curenv->env_tf.tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
		curenv->env_tf.tf_esp = (uintptr_t)utr;
		env_run(curenv);			//重新进入用户态
	}

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

