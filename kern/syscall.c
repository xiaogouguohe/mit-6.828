/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	// envid_t envid = sys_getenvid();
	user_mem_assert(curenv, s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *new_env = NULL;
	envid_t res = 0;

	/* 无法正确分配新环境，直接返回 */
	if ((res = env_alloc(&new_env, curenv->env_id)) != 0) {
		return res;
	}

	/* 新环境运行状态设为 NOT_RUNNABLE */
	new_env->env_status = ENV_NOT_RUNNABLE;

	/* 复制父进程当前的寄存器值 */
	new_env->env_tf = curenv->env_tf;
	new_env->env_tf.tf_regs.reg_eax = 0;

	return new_env->env_id;
	// panic("sys_exofork not implemented");
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env* e = NULL;
	int res = 0;

	/* 需要检测当前环境是否有权限对 环境 envid 设置运行状态，因此标志位为 1 */
	if ((res = envid2env(envid, &e, 1)) != 0) {
		return res;
	}

	/* 检测设置的运行状态是否合法 */
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}

	e->env_status = status;
	return 0;

	// panic("sys_env_set_status not implemented");
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	panic("sys_env_set_trapframe not implemented");
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
/* 当环境 envid 发生页异常时，跳转到函数 func 进行处理

注意，这里只是把环境 e 的用户态页异常的处理函数 env_pgfault_upcall 设为了func，并没有执行

这种页错误，和内核态的 page_fault_handler 处理函数不一样，
那个是处理不正常的情况的，而这个是处理正常情况的 */
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	// struct Env* e = NULL;
	// int res = envid2env(envid, &e, 1);
	// if (res < 0) {
	// 	return -E_BAD_ENV;
	// }

	// e->env_pgfault_upcall = func;

	// return 0;
    	struct Env *env;
	int ret;
	if ((ret = envid2env(envid, &env, 1)) < 0) {
		return ret;
	}
	env->env_pgfault_upcall = func;
	return 0;

	// panic("sys_env_set_pgfault_upcall not implemented");
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
/* 分配一页物理内存，并将其映射到环境 envid 的虚拟地址空间中的虚拟地址 va */
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct Env* e = NULL;

	if (envid2env(envid, &e, 1) != 0) {
		return  -E_BAD_ENV;
	}

	if ((uint32_t) va >= UTOP || (uint32_t) va % PGSIZE != 0) {
		return -E_INVAL;
	}
	if((perm & PTE_SYSCALL) == 0){
		return -E_INVAL;
	}
    if (perm & ~PTE_SYSCALL) {
		return -E_INVAL;
	}

	struct PageInfo *pp = page_alloc(1);
	if (!pp) {
		return -E_NO_MEM;
	}

	int res = page_insert(e->env_pgdir, pp, va, perm);
	if (res < 0) {
		/* 记得释放页 */
		page_free(pp);
		return -E_NO_MEM;
	}

	return 0;
	//panic("sys_page_alloc not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL if srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
/* 将环境 srcenvid 中的虚拟地址 srcva 对应的物理页（注意不是拷贝页里面的内容！）
	映射到环境 dstenvid 的虚拟地址 dstva 上
    
    perm 表示环境 dstenvid 要求的对这个物理页的权限 */
static int
sys_page_map(envid_t srcenvid, void *srcva, envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env* srce = NULL,  *dste = NULL;
    // cprintf("000\n");

	if (envid2env(srcenvid, &srce, 1) != 0) {
		return  -E_BAD_ENV;
	}
	if (envid2env(dstenvid, &dste, 1) != 0) {
		return  -E_BAD_ENV;
	}
    // cprintf("111\n");

	if ((uint32_t) srcva >= UTOP || PGOFF(srcva) || (uint32_t) dstva >= UTOP || PGOFF(dstva)) {
		return -E_INVAL;
	}

	if((perm & PTE_SYSCALL) == 0){
		return -E_INVAL;
	}
    // cprintf("222\n");

    if (perm & ~PTE_SYSCALL) {
		return -E_INVAL;
	}

	/* 找到虚拟地址 srcva 对应的物理页 */
	pte_t *ptep = NULL;
	struct PageInfo *pp = page_lookup(srce->env_pgdir, srcva, &ptep);
	/* 虚拟地址 srcva 没有映射到物理页上 */
	if (!pp) {
        // cprintf("sys_page_map, lookup fail\n");
		return -E_INVAL;
	}

	/* 这个系统调用要求可写权限，但 srcva 却只有只读权限 */
	if ((perm & PTE_W) && (*ptep & PTE_W) == 0) {
        // cprintf("ask for write, but only has read perm\n");
		return -E_INVAL;
	}

	/* 建立 dste 的虚拟地址 dstva 和物理页 pp 的映射关系，注意dst 和 src 区分 */
	int res = page_insert(dste->env_pgdir, pp, dstva, perm);
	if (res < 0) {
        // cprintf("page_insert fail\n");
		return -E_NO_MEM;
	}

	return 0;

	// panic("sys_page_map not implemented");
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env* e = NULL;

	if (envid2env(envid, &e, 1) != 0) {
		return  -E_BAD_ENV;
	}

	if ((uint32_t) va > UTOP || PGOFF(va)) {
		return -E_INVAL;
	}

	page_remove(e->env_pgdir, va);


	return 0;
	// panic("sys_page_unmap not implemented");
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
    struct Env* dste = NULL;
    /* 权限检查是检查环境 dste 是否为当前环境或者当前环境的子环境，
    这里没有这个限制 */
	if (envid2env(envid, &dste, 0) != 0) {
        cprintf("sys_send error because of bad env\n");
		return  -E_BAD_ENV;
	}

    // ？？？
    if (!dste->env_ipc_recving/* || dste->env_status != ENV_NOT_RUNNABLE || dste->env_ipc_from != 0*/) {
        return -E_IPC_NOT_RECV;
    }

    int ret = 0;

    if ((uint32_t) srcva < UTOP) {
        /* 虚拟地址 srcva 没有页对齐 */
        if (PGOFF(srcva) != 0) {
            cprintf("sys_send error because of page not alien\n");
            return -E_INVAL;
        }

        // cprintf("sys_ipc_try_send, begin to sys_page_map\n");
        // if ((ret = sys_page_map(curenv->env_id, srcva, envid, dste->env_ipc_dstva, perm)) < 0) {
        //     return ret;
        // }

        /* 找到虚拟地址 srcva 对应的物理页 */
        pte_t* ptep = NULL;
        struct PageInfo* page_info = page_lookup(curenv->env_pgdir, srcva, &ptep);

        /* perm应该是*pte的子集 */
        if ((*ptep & perm) != perm) {
            return -E_INVAL;					
        }

        /* 虚拟地址 srcva 还没映射到物理页 */
        if (!page_info) {
            return -E_INVAL;
        }

        /* 接收者要求映射到虚拟地址 dstva 的物理页的写权限，
        但事实上接收者又不具有写权限 */
        if ((perm & PTE_W) && !(*ptep & PTE_W)) {
            return -E_INVAL;
        }

        /*  */
        if ((uint32_t )(dste->env_ipc_dstva) < UTOP) {
            ret = page_insert(dste->env_pgdir, page_info, dste->env_ipc_dstva, perm); //共享相同的映射关系
            if (ret) {
                return ret;
            } 
            dste->env_ipc_perm = perm;
        }
    }

    // struct Env *dste;
	// int ret = envid2env(envid, &dste, 0);
	// if (ret) return ret;
	// if (!dste->env_ipc_recving) return -E_IPC_NOT_RECV;

	// if (srcva < (void*)UTOP) {
	// 	pte_t *pte;
	// 	struct PageInfo *pg = page_lookup(curenv->env_pgdir, srcva, &pte);

	// 	//按照注释的顺序进行判定
	// 	if (srcva != ROUNDDOWN(srcva, PGSIZE)) return -E_INVAL;		//srcva没有页对齐
	// 	if ((*pte & perm) != perm) return -E_INVAL;					//perm应该是*pte的子集
	// 	if (!pg) return -E_INVAL;									//srcva还没有映射到物理页
	// 	if ((perm & PTE_W) && !(*pte & PTE_W)) return -E_INVAL;		//写权限
		
	// 	if (dste->env_ipc_dstva < (void*)UTOP) {
	// 		ret = page_insert(dste->env_pgdir, pg, dste->env_ipc_dstva, perm); //共享相同的映射关系
	// 		if (ret) return ret;
	// 		dste->env_ipc_perm = perm;
	// 	}
	// }

    // // cprintf("sys_ipc_try_send success\n");
    dste->env_ipc_recving = false;
    dste->env_ipc_from = curenv->env_id;
    dste->env_ipc_value = value;
    dste->env_status = ENV_RUNNABLE;

    /* ？？？ */
    dste->env_tf.tf_regs.reg_eax = 0;

    return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
    curenv->env_ipc_recving = true;
    curenv->env_ipc_dstva = dstva;
    curenv->env_status = ENV_NOT_RUNNABLE;
    // cprintf("sys_ipc_recv, before yield\n");
    sys_yield();
    // cprintf("sys_ipc_recv, after yield\n");

    if ( (uint32_t) dstva < UTOP) {
        if ((uint32_t) dstva % PGSIZE != 0) {
            return -E_INVAL;
        }   
    }

	return 0;

	// panic("sys_ipc_recv not implemented");
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");

	/* 根据系统调用号，调用具体的系统调用，
	参数就是寄存器的值a1 ~ a5 */
	switch (syscallno) {
	case (SYS_cputs):
		sys_cputs((const char *)a1, a2);
		return 0;
	case (SYS_cgetc):
		return sys_cgetc();
	case (SYS_getenvid):
		return sys_getenvid();
	case (SYS_env_destroy):
		return sys_env_destroy((envid_t) a1);
	case (SYS_yield):
		sys_yield();
		return 0;
	case (SYS_exofork):
		return sys_exofork();
	case (SYS_env_set_status):
		return sys_env_set_status((envid_t) a1, (int) a2);
	case (SYS_page_alloc):
		return sys_page_alloc((envid_t) a1, (void*) a2, (int) a3);
	case (SYS_page_map):
		return sys_page_map((envid_t) a1, (void*) a2, (envid_t) a3, (void*) a4, (int) a5);
	case (SYS_page_unmap):
		return sys_page_unmap((envid_t) a1, (void*) a2);
    case (SYS_env_set_pgfault_upcall):
        return sys_env_set_pgfault_upcall((envid_t) a1, (void*) a2);
    case (SYS_ipc_recv):
        return sys_ipc_recv((void*) a1);
    case (SYS_ipc_try_send):
        return sys_ipc_try_send((envid_t) a1, a2, (void*)a3, a4);
	default:
		return -E_INVAL;
	}
}


