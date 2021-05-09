// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    /* 页异常需要满足
    1) 是写引起的，因此需要判断该页是否可写
    2) 是 cow 页 */
    if ((err & FEC_WR) == 0 || (uvpt[PGNUM(addr)] & PTE_P) == 0 || (uvpt[PGNUM(addr)] & PTE_COW) == 0) {
        panic("pgfault: it's not writable or attempt to access a non-cow page!");
    }    

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    envid_t envid = sys_getenvid();
    if ((r = sys_page_alloc(envid, (void*) PFTEMP, PTE_P | PTE_U |PTE_W)) < 0) {
        panic("pgfault: page allocation failed %e", r);
    }

    addr = ROUNDDOWN(addr, PGSIZE);
    /* 把发生用户态页异常的虚拟地址 addr 的内容，移动到虚拟地址 PFTEMP */
    memmove(PFTEMP, addr, PGSIZE);

    /* 解除虚拟地址 addr 和物理地址的映射
    
    其实应该不用 unmap 的，map 的时候会自动解除之前的映射关系 */
    // if ((r = sys_page_unmap(envid, addr)) < 0) {
    //     panic("pgfault: page unmap failed %e", r);
    // }

    /* 建立 
    1) 虚拟地址 addr ，和 
    2) 虚拟地址 PFTEMP 映射到的物理页
    的映射关系 */
    if ((r = sys_page_map(envid, PFTEMP, envid, addr, PTE_P | PTE_W |PTE_U)) < 0) {
        panic("pgfault: page map failed %e", r);
    }
    /* 解除虚拟地址 PFTEMP 和物理地址的映射关系 */
    if ((r = sys_page_unmap(envid, PFTEMP)) < 0) {
        panic("pgfault: page unmap failed %e", r);
    }

	// panic("pgfault not implemented");

}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
    pte_t pte_p = uvpt[pn];
    void* addr = (void*)(pn * PGSIZE);

    /* perm 表示环境 envid 对虚拟地址 pn * PGSIZE 要求的权限

    首先，肯定要存在，且要求用户权限 */
    int perm = PTE_P | PTE_U;
    /* 如果父环境，也就是当前环境，对虚拟地址 pn*PGSIZE 具有写权限或者是 cow，
    环境 envid 还需要要求 cow */
    if ((pte_p & PTE_W) || (pte_p & PTE_COW)) {
        perm |= PTE_COW;
    }

    /* 把当前环境和环境 envid 的虚拟地址 addr 都映射到同一个物理地址 */
    envid_t cur_envid = sys_getenvid();
    if ((r = sys_page_map(cur_envid, (void*) addr, envid, (void*) addr, perm))) {
        panic("duppage: page remapping failed %e", r);
    }

    /* 为什么要重新映射？？？ */
    if (perm & PTE_COW) {
        if ((r = sys_page_map(cur_envid, (void*) addr, cur_envid, (void*) addr, perm)) < 0) {
            panic("duppage: page remapping failed %e", r);
            return r;
        }
    }

	// panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
    /* 不确定父环境，也就是当前环境是否已经设置了用户态页异常的处理函数，先设置 */
    extern void _pgfault_upcall(void);
    set_pgfault_handler(pgfault);

    envid_t child_envid = sys_exofork();
    if (child_envid < 0) {
        panic("sys_exofork failed: %e", child_envid);
        return child_envid;    
    }

    if (child_envid == 0) {
        /* 修改子环境的 thisenv */
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }

    int r = 0;
    /* 遍历从虚拟地址 UTEXT 到 UXSTACKTOP 的每个页 */

    /* 遍历 UXSTACKTOP 的每个页 */
	for (uint32_t addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) // 这个页的页表在页目录中存在
            && (uvpt[PGNUM(addr)] & PTE_P) // 这个页在页表中存在
			&& (uvpt[PGNUM(addr)] & PTE_U)) { // 当前环境对这个页有用户权限
            /* 拷贝当前进程映射关系到子进程 */
			duppage(child_envid, PGNUM(addr));	
		}
	}

    /* 给子环境的用户态错误栈分配物理空间 */
    if ((r = sys_page_alloc(child_envid, (void *)(UXSTACKTOP-PGSIZE), PTE_U | PTE_P | PTE_W)) < 0) {
        return r;
    } 

    /* 环境 child_envid 的用户态错误栈的虚拟地址已经映射到一个物理地址，
    现在要把这个物理地址址 PFTEMP 和子环境用户态错误栈的物理页的映射关系 */
    if ((r = sys_page_unmap(thisenv->env_id, PFTEMP)) < 0) {
        panic("fork: page unmap failed %e", r);
        return r;
    }

    /* 为子环境设置用户态页异常错误处理函数 */
    if ((r = sys_env_set_pgfault_upcall(child_envid, _pgfault_upcall)) < 0) {
        return r;
    }

    // Start the child environment running
    /* 把子环境设为就绪态 */
    if ((r = sys_env_set_status(child_envid, ENV_RUNNABLE)) < 0) {
        panic("sys_env_set_status: %e", r);
    }    

    return child_envid;
    
	// // panic("fork not implemented");
    // // LAB 4: Your code here.
	// extern void _pgfault_upcall(void);
	// set_pgfault_handler(pgfault);	//设置缺页处理函数
	// envid_t envid = sys_exofork();	//系统调用，只是简单创建一个Env结构，复制当前用户环境寄存器状态，UTOP以下的页目录还没有建立
	// if (envid == 0) {				//子进程将走这个逻辑
	// 	thisenv = &envs[ENVX(sys_getenvid())];
	// 	return 0;
	// }
	// if (envid < 0) {
	// 	panic("sys_exofork: %e", envid);
	// }

	// uint32_t addr;
	// for (addr = 0; addr < USTACKTOP; addr += PGSIZE) {
	// 	if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) //为什么uvpt[pagenumber]能访问到第pagenumber项页表条目：https://pdos.csail.mit.edu/6.828/2018/labs/lab4/uvpt.html
	// 		&& (uvpt[PGNUM(addr)] & PTE_U)) {
	// 		duppage(envid, PGNUM(addr));	//拷贝当前进程映射关系到子进程
	// 	}
	// }
	// int r;
	// if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_P | PTE_W | PTE_U)) < 0)	//为子进程分配异常栈
	// 	panic("sys_page_alloc: %e", r);
	// sys_env_set_pgfault_upcall(envid, _pgfault_upcall);		//为子进程设置_pgfault_upcall

	// if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)	//设置子进程为ENV_RUNNABLE状态
	// 	panic("sys_env_set_status: %e", r);
	// return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}


