# 第4章 抢占式多任务

## 4.1 Introduction

- 在本实验中，需要支持用户环境之间的并发和抢占式任务处理

  - Part A中
    - 为JOS添加多处理器支持，实现循环调度
    - 添加管理环境的系统调用，实现创建和销毁环境以及分配/映射内存
  - Part B中，实现一个类似Unix的fork()，允许用户环境创建其自身的副本

  - Part C中
    - 添加对进程间通信（IPC）的支持
    - 添加对硬件时钟中断和抢占的支持

## 4.2 Part A: 对多处理器的支持和多任务之间的协作

- 使JOS支持多处理器
- 实现一些系统调用，允许用户环境创建其他新环境
- 实现协作式调度，即当前进程自愿放弃CPU时，允许内核从一个环境切换到另一个
  - Part C需要实现抢占式调度，即内核在一定时间后剥夺某个环境对CPU的控制，即使这个环境没有主动退出

### 4.2.1  对多处理器的支持

- 目标是使JOS支持SMP
  - SMP是一种多处理器模型，所有CPU共享系统资源（内存，I/O总线等等）
  - 启动阶段，这些CPU被分为两类
    - 引导处理器（BSP）负责初始化系统和引导操作系统
    - 只有在操作系统启动并运行后，BSP才会激活应用程序处理器（AP）
    - BSP处理器是由硬件和BIOS决定的。到目前为止，所有现有的JOS代码都已在BSP上运行
- SMP系统中，每个CPU都附带一个本地APIC（LAPIC）
  - 负责在整个系统中传递中断
  - 为其连接的CPU提供唯一标识符
  - 本实验中，使用LAPIC的以下功能（在kern/lapic.c）
    - 根据LAPIC的ID，区别代码运行在哪个CPU上，通过` cpunum() `来实现
    - 从BSP向AP发送`STARTUP`处理器间中断（IPI）去唤醒其他的CPU，通过 `lapic_startap()` 实现
    - 在Part C，编写LAPIC的内置定时器来触发时钟中断，以支持抢占式多任务，通过 `pic_init()` 实现
- CPU通过内存映射的MMIO访问它的LAPIC
  - 在MMIO中，一部分物理内存被硬连线到某些I/O设备的寄存器
    - 例如0x000a0000上的I/O hole，它用来连接VGA显示缓冲区
    - 对这些内存的加载/存储指令可用于访问设备寄存器
  - LAPIC位于从物理地址0xfe000000开始的孔中，也就是比4GB小32MB
    - 这个物理地址太高，无法通过之前的直接映射来访问到它，之前是虚拟地址KERNBASE，也就是0xf0000000以上的256MB映射到物理地址0x0
    - 虚拟地址空间的MMIOBASE以上的4MB空间，用于映射这个IO hole
- Code: 实现 `kern/pmap.c` 的 `mmio_map_region` 
  - `kern/lapic.c` 中的 `lapic_init` 用到了这个函数
  - 还需要实现下一个Code，才能通过 `mmio_map_region` 的测试
  - Code hint
    - 注意对齐
    - 注意权限
    - 注意 `base` 要随之增加
    - PCD、PWT 权限？？？

#### 4.2.1.1 处理器引导程序的应用

- 在启动AP之前，BSP应当收集多处理器系统的信息

  - 例如CPU总数，它们的APIC ID，MMIO地址等等
  - `kern/mpconfig.c` 中的 `mp_init()` 函数通过读取驻留在BIOS内存区域中的MP配置表，来获取这些信息
    - 出厂时，厂家就把这些处理器信息写入了BIOS中，由 `kern/mpconfig.c` 定义
- ` kern/init.c` 中的 `boot_aps()` 函数驱动AP的引导过程

  - AP在实模式下启动，就像boot loader在 `boot/boot.S` 中启动的方式一样

  - 因此 `boot_aps()` 将AP入口代码 `kern/mpentry.S` 复制到可在实模式下寻址的内存位置

    - BIOS把boot loader加载到物理地址0x00007c00，而实模式下能寻址的物理内存是低1MB（0x00010000），包括了0x00007c00
    - 和 boot loader 不同的是，可以控制AP于何处开始执行代码，而不是固定位置，在JOS中是把 `kern/mpentry.S` 复制到0x00007000（ `MPENTRY_PADDR` ），但是事实上低于640KB的任何未使用的，页对齐的物理地址都是可以的

  - 之后，`boot_aps()` 函数遍历 `cpus` 数组，发送 `STARTUP` 的IPI（处理器间中断）信号到AP的 LAPIC 单元来一个个地激活AP

    - 在 `kern/mpentry.S` 中的入口代码跟 `boot/boot.S` 中的代码类似

  - AP 被激活后会执行 `kern/mpentry.S` 中的代码，在一些简短的配置后，它使AP 进入开启分页机制的保护模式，调用 C 语言的 setup 函数 `mp_main()` 

    - boot_aps 等待AP在其结构CpuInfo的cpu_status字段中发出CPU_STARTED标志信号，然后再唤醒下一个

    - 该函数为当前AP设置GDT，TTS，最后设置cpus数组中当前CPU对应的结构的cpu_status为CPU_STARTED
- Code: 修改 `page_init()` ，以避免将 `MPENTRY_PADDR` 中的页面添加到空闲列表中，这样才能把AP引导程序代码加载到这个物理地址

  - 需要阅读 `kern/init.c` 的 `boot_aps()` 和 `mp_main()` ，以及 `kern/mpentry.S` 的汇编代码，来了解AP引导过程中的控制是如何转移的
  - 代码应当通过新的 `check_page_free_list()` 测试，但是可能无法通过 `check_kern_pgdir()` 测试，需要完成后面的实验才能通过
  - 为什么要把这个页面标记为已使用，不会覆盖 0x7c00 的 boot loader 吗？？？
  - 为什么 boot loader 的时候不用标记为已使用？？？
- Question: 比较 `kern/mpentry.S` 和 `boot/boot.S` 
  - `kern/mpentry.S` 就像内核的其他内容一样，经过编译和链接之后，在 `KERNBASE` 上运行
  - 宏 `MPBOOTPHYS` 的目的是什么？ 为什么在 `kern/mpentry.S` 中有必要，但在 `boot / boot.S` 中却没有必要？
    - 回忆一下在实验1中讨论的链接地址和加载地址之间的区别
    - `boot.S` 中，没有启用分页机制，所以能够指定程序开始执行的地方以及程序加载的物理地址
    - 但是，在mpentry.S的时候，主CPU已经处于保护模式下了，因此不能直接指定物理地址，就需要给定线性地址，再通过 `MPBOOTPHYS` 映射到相应的物理地址
    - 为什么不能用 `KADDR` ？？？

#### 4.2.1.2 Per-CPU State and Initialization

- 实现多处理器OS时，需要区分每个处理器专用的 per-CPU 状态和整个系统共享的全局状态

  - `kern/cpu.h` 定义了大多数 per-CPU 状态，包括存储 per_CPU 变量 `CpuInfo` 结构
  - `cpunum()` 返回调用它的 CPU ID，这个 ID 可以作为 `cpus` 数组的索引
  - 宏 `thiscpu` 是当前 CPU 的 `CpuInfo` 结构

- 应当注意的 per-CPU 状态

  - per-CPU 内核栈
    - 多个 CPU 可以同时切换到内核态，因此每个 CPU 都要有单独的内核堆栈
    - 数组 `percup_kstacks[NCPU][KSTKSIZE]` 为 NCPU 个 CPU 保留了内核栈的空间
    - Lab 2 中，映射了单个 CPU 的内核栈，在虚拟地址为 `KSTACKTOP` ，大小为 `KSTKSIZE` ，物理地址见Lab 1；在这个实验，需要把多个 CPU 的内核栈映射到这个区域，每个 CPU 的内核栈相隔 `KSTKGAP` ，内存布局见 `inc/memlayout.h` 
  - per-CPU TSS 和 TSS 描述符
    - per-CPU 的 TSS 指定 per-CPU 内核栈的位置
      - 3.2.5.2 已经提到了这一点
    - CPU i 的 TSS 存储在 `cpus[i].cpu_ts` 中，TSS 的段描述符在 GDT 项 `gdt[(GD_TSS >>3) + i]` 定义
      - Lab 3 中定义的全局变量 `ts` 不再有用
  - per-CPU 当前环境指针
    - 每个 CPU 可以同时运行不同的用户进程，因此可以把当前 CPU 的当前用户进程表示为 `cpus[cpunum()].cpu_env` 或 `thiscpu->cpu_env` 
      - Lab 3 中定义的 `cur_env` 不再有用

  - per-CPU 寄存器
    - 所有寄存器，包括系统寄存器，都是 CPU 专用的
    - 因此，初始化这些寄存器的指令，如 `lcr3()` , `ltr()` , `lgdt()` , `lidt()` 等，必须在每个 CPU 上执行一次
    - `env_init_percpu()` 和 `trap_init_percpu()` 实现了这些
  - 除此之外，如果在之前添加了任何额外的 per-CPU 状态，或者执行了任意 CPU 的初始化，如在 CPU 寄存器中设置新位，要确保复制到这里的每个 CPU 上

- Code: 修改 ` kern/pmap.c` 中的 `mem_init_mp()` ，以映射从 `KSTACKTOP` 开始的每个 CPU 堆栈，每个堆栈的大小为 `KSTKSIZE` 字节，互相间隔 `KSTKGAP` 字节
  - `kern/pmap.c` 的 `mem_init` 会调用这个函数，因为这个函数是初始化内存布局的
  - 要通过 `check_kern_pgdir()` 检查
  - Code hint
    - 注意每个内核栈映射的范围
- Code: 修改 `kern/trap.c` 的 `trap_init_percpu()` ，以正确初始化 BSP CPU 的 TSS 和 TSS 段描述符
  - 原来的代码在 Lab 3 中可以正常工作，但是在其它 CPU 上运行时不正确，现在要使得它在所有 CPU 上都正常运行
  - 不能再使用变量 `ts`
  - 事实上还需要在 `i386_init()` 获取锁再调用 `boot_aps()` ，否则 `sched_yield()` 调用 `sched_halt()` ，会多释放一次锁
    - 这个原来是应该在 4.2.1.3 做的
  - boot_main启动了每个 AP
    - 如果只是 `make qemu` ，实际上 `boot_main` 不会进入启动 AP 的循环，也就不会调用 `mp_main()` ，因为此时 CPU 数量只有 1，没有其它 CPU

#### 4.2.1.3 锁

- `mp_main()` 初始化 AP 后，AP 的代码会自旋（死循环）
  - AP 执行到 `mp_main()` 最后的 `for` 死循环
  - BSP 执行到 `sched_yield()` 最后的 `sched_halt()` 
    - 如果当前没有环境处于运行，就绪或僵死状态，当前 CPU 陷入内核
- 在使得 AP 能继续运行之前，需要解决多个 CPU 同时运行内核代码时的竞争
  - 使用大内核锁来解决这个问题
    - 单个全局锁，每个环境进入内核态时都持有该锁，并在返回用户态时释放该锁
    - 这样，用户态下的环境可以在任何可用的 CPU 上运行，但是内核态下不能超过一个环境
  - `kern/spinlock.h` 声明大内核锁 `kernel_lock` ，并提供 `lock_kernel()` 和 `unock_kernel()` 
  - 需要在以下几个位置应用大内核锁
    - `i386_init()` 中，在 BSP 唤醒其它 CPU 之前获取锁
      - thougths: 它觉得这里算切换到内核态了，因此获取内核锁，事实上获取内核锁的时机是否只能在这里？？？
    - `mp_main()` 中，在初始化 AP 之后获取锁·，然后调用`sched_yield() `开始在此 AP 上运行环境
    - `trap()` 中，从用户态陷入内核态时获取锁
      - 要确定是从用户态陷入的还是原来就在内核态，要检查 `tf_cs` 的低位
    - `env_run()` 中，切换到用户态前释放锁
      - 不要太早或太晚这样做，否则会发生数据冲突或死锁
- Code: 在上述几个位置应用大内核锁
  - 此时还无法测试代码正确性，在完成了后面的调度策略之后才可以测试，现在会在 `sched.c` 的 `sched_halt()` 处阻塞
- Question: 大内核锁可以保证一次只有一个 CPU 运行内核代码，为什么每个CPU仍然需要单独的内核栈？
  - 会覆盖掉之前的内核栈的数据，这取决于一个环境退出内核态时，会不会把内核栈的所有数据弹出？？？
  - `_alltrap()` 到 `lock_kernel()` 中间的过程？？？

### 4.2.2 Round-Robin Scheduling（循环调度）

- 现在要更改 JOS 内核，使得它可以“循环”在多个环境之间切换，工作方式如下
  - `kern/sched.c` 中的 `sched_yield()` 选择要运行的新环境，以循环方式搜索 `envs[]` 数组，从先前运行的环境开始（如果之前没有运行的环境，就从第一个环境开始），选择状态为 `ENV_RUNNABLE` 的第一个环境，（参考 `inc/env.h` ），然后调用 `env_run()` 切换到该环境
  - `sched_yield()` 不能在两个 CPU 上运行同一个环境，为了避免这一点，环境的状态为 `ENV_RUNNING` ，则表示该环境在某些 CPU 上运行
  - 已经实现了系统调用 `sys_yield()` ，用户环境可以调用该系统调用来调用内核的 `sched_yield()` ，从而自动将 CPU 放弃，让这块 CPU 运行其他环境

- Code: 如上所述，在 `sched_yield()` 中实现轮make询调度

  - 要修改 `syscall()` 来调用 `sys_yield()` ，再调用 `sched_yield()` 

  - 确保在 `mp_main()` 中调用到 `sched_yield()` 

  - 修改 `kern/init.c` 以创建三个（或更多）环境，这些环境都运行程序 `user/yield.c` 

- 测试

  - 运行 `make qemu` ，在终止之前，应该看到环境来回切换了五次
    - `user/yield.c` 执行了五次 `sys_yield()` 

  - 使用多个 CPU 进行测试： `make qemu CPUS=2` 
  - 在yield程序退出之后，系统中将没有可运行的环境，调度程序应调用 JOS 内核监视器.如果以上任何一种情况均未发生，请在继续操作之前先修复代码

- Question: 在 `env_run()` 中调用了 `lcr3()` ，而在修改 `cr3` 寄存器，也就是切换页表前后前后，都引用了变量 `e` ，切换页表之后不会使得 `e` 变化吗？

  - 回忆虚拟地址空间的内存布局，在 `UTOP` 之上的部分，只有 `UVPT` 到 `ULIM` 是每个进程页表会不同的，而 `e` 在 `KERNBASE` 之上，自然是不会受切换页表的影响的

- 每当内核切换环境时，都必须保存旧环境的寄存器，以便以后可以正确还原它们，保存旧环境的寄存器在哪里发生？

  - 切换环境要进入内核态，而一旦切换到内核态（不管是因为切换环境还是其它什么原因），都需要保存用户栈的内容
  - 而如果是切换环境导致的陷入内核，那么旧环境的寄存器值就会保存到这个环境的内核栈了
  - 保存的工作在 `trap()` 完成，是保存到环境的 `tf` 结构，为什么内核栈有了还需要在这里保存一份？？？什么时候写进内核栈的？？？
  - sys_yield(lib) -> syscall(lib) -> trap() -> syscall(kern) -> sys_yield(kern) -> sched_yield(kern)

### 4.2.3 创建环境的系统调用

- 现在内核可以在多个用户环境中进行切换和运行，但是这些环境都是内核最开始创建的，现在实现系统调用，以允许用户环境创建和启动新的用户环境

- Unix 提供 `fork()` 系统调用，作为创建环境的原语

  - 复制父进程的地址空间，以创建一个子进程
  - 复制完成后，从用户空间观察到的它们的唯一区别是进程 ID
  - 父进程返回子进程 ID，而子进程返回0
  - 每个进程有自己的地址空间，因此一个进程对内存的修改对其它进程不可见（写时复制）

- 在这里需要实现一组更原始的 JOS 系统调用，以创建新的用户态环境，这些系统调用如下

  - `sys_exofork()`  

    - 创建了一个几乎空白的用户环境，其地址空间的用户态部分未映射任何内容，且该环境不可运行
    - 创建的新环境和父环境有相同的寄存器状态
    - 父环境返回子环境 ID，子环境返回 0 
    - 由于子环境在开始时标记为不可运行，因此 `sys_exofork` 不会真正在子环境中返回，直到父进程标记子进程可显式允许此操作？？？通过什么标记？？？

  - `sys_env_set_status()` 

    - 将指定环境的状态设置为 `ENV_RUNNABLE` 或 `ENV_NOT_RUNNABLE` 

    - 此系统调用通常用于标记准备运行的新环境，一旦其地址空间和寄存器状态已完全初始化，该环境就可以运行

  - `sys_page_alloc()` 

    - 分配一页物理内存，并将其映射到给定环境的地址空间中的给定虚拟地址

  - `sys_page_map()` 

    - 将页面映射关系（不是页面的内容！）从一个环境的地址空间复制到另一个环境，保留内存共享安排，以便新映射和旧映射都引用同一物理内存页面。

  - `sys_page_unmap()` 

    - 解除映射关系

- 对于上面所有系统调用，它们都有如下约定，即接受的环境 ID 参数如果为 0，表示当前环境

  - 参考 `kern/env.c` 的 `envid2env()` 

- 测试程序 `user/dumbfork.c` 中提供了一个类似于 `fork()` 的实现，

  - 该测试程序使用上述系统调用来创建和运行带有其自身地址空间副本的子环境
  - 然后，像前面的练习一样，使用 `sys_yield()` 在两个环境之间来回切换
  - 父环境10次迭代后退出，而子环境在20次迭代后退出。

- Code: 在 `kern/syscall.c` 中实现上述系统调用，并确保 `syscall()` 对其进行调用

  - 需要在 `kern/pmap.c` 和 `kern/env.c` 中使用各种功能，尤其是 `envid2env()` 
  - 现在，无论何时调用 `envid2env()` ，都要在checkperm参数中传递 1
  - 确保检查所有无效的系统调用参数，在这种情况下返回 `-E_INVAL` 
  - 使用 `user/dumbfork` 测试JOS内核，并在继续操作之前确保其工作正常
    - 注意根据 `user/dumbfork` 程序来分析这些函数调用如何使用，熟悉整个过程
  - 通过 `make grade` 测试
  - `sys_exofork()` 怎么实现返回两次的？？？

## 4.3 Part B: 写时复制



