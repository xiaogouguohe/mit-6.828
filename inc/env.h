/* See COPYRIGHT for copyright information. */

#ifndef JOS_INC_ENV_H
#define JOS_INC_ENV_H

#include <inc/types.h>
#include <inc/trap.h>
#include <inc/memlayout.h>

typedef int32_t envid_t;

// An environment ID 'envid_t' has three parts:
//
// +1+---------------21-----------------+--------10--------+
// |0|          Uniqueifier             |   Environment    |
// | |                                  |      Index       |
// +------------------------------------+------------------+
//                                       \--- ENVX(eid) --/
//
// The environment index ENVX(eid) equals the environment's index in the
// 'envs[]' array.  The uniqueifier distinguishes environments that were
// created at different times, but share the same environment index.
//
// All real environments are greater than 0 (so the sign bit is zero).
// envid_ts less than 0 signify errors.  The envid_t == 0 is special, and
// stands for the current environment.

#define LOG2NENV		10
#define NENV			(1 << LOG2NENV)
#define ENVX(envid)		((envid) & (NENV - 1))

// Values of env_status in struct Env
enum {
	ENV_FREE = 0,
	ENV_DYING,
	ENV_RUNNABLE,
	ENV_RUNNING,
	ENV_NOT_RUNNABLE
};

// Special environment types
enum EnvType {
	ENV_TYPE_USER = 0,
};

struct Env {
	/* inc/trap.h定义，存放当环境上下文切换时，重要寄存器的值，
	注意和过程（函数）调用时的寄存器保存进行比较，
	过程调用时，寄存器是保存在用户栈的 */
	struct Trapframe env_tf;	// Saved registers
	/* 指向在env_free_list中，后一个free的Env结构体，
	当前结构体空闲时，该域才有用 */
	struct Env *env_link;		// Next free Env
	/* 唯一标识 */
	envid_t env_id;			// Unique environment identifier
	/* 父环境id */
	envid_t env_parent_id;		// env_id of this env's parent
	/* 区别某些特殊的环境，大多数时候都是ENV_TYPE_USER */
	enum EnvType env_type;		// Indicates special system environments
	/* 环境的状态，有以下可能，
	ENV_FREE: 结构体在env_free_list中
　ENV_RUNNABLE: 用户环境就绪，等待被分配处理机
　ENV_RUNNING: 用户环境正在运行
　ENV_NOT_RUNNABLE: 用户环境阻塞，等待其他环境的信号
　ENV_DYING: 僵尸环境 */
	unsigned env_status;		// Status of the environment

	uint32_t env_runs;		// Number of times environment has run

	// Address space
	/* 该环境的页目录的虚拟地址,
	每个环境的虚拟地址空间是独立的，因此要有各自的页目录和页表 */
	pde_t *env_pgdir;		// Kernel virtual address of page dir
};

#endif // !JOS_INC_ENV_H
