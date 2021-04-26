# Lab 2 Memory Management

## 1 简介

- 本实验需要完成内存管理的代码，它分为两个模块

  - 第一个模块是内核的物理内存分配器，以便内核可以分配和释放内存
    - 以页为单位分配，页大小为4096字节
    - 需要维护一个数据结构，来记录哪些物理页面是空闲的，哪些是已分配的，以及多少进程正在共享每个分配的页面，还要编写例程来分配和释放内存页面。

  - 第二个模块是虚拟内存，将虚拟地址映射到物理地址
    - x86硬件的内存管理单元（MMU）通过查阅页表进行映射
    - 需要根据提供的规范修改JOS以设置MMU的页表

### 1.1 如何开始Lab 2

- 需要合并Lab 1的代码，这里采用git rebase

### 1.2 Lab Requirements

- ###

### 1.3 提交步骤

- ###

## 2 第一部分：物理页的管理

- 操作系统要跟踪物理RAM的哪些部分是空闲的，哪些部分是正被使用的
  - JOS以页面为粒度管理物理内存
- 现在，需要实现物理页分配器
  - struct PageInfo跟踪哪些页面是空闲的
  - 需要实现kern/pmap.c的以下代码
    - boot_alloc()
    - mem_init()
    - page_init()
    - page_alloc()
    - page_free()
  - check_page_free_list() 和 check_page_alloc() 测试物理页面分配器
    - 测试方法是，启动JOS并查看 check_page_alloc()是否报告成功
    - assert() 在调试过程中是一种有用的手段
- 这个实验和后面的实验都没有非常详细地描述要做什么，需要通过查找注释等方法来获取这些信息
  - 实验代码和注释见代码仓库

## 3 第二部分：虚拟内存

- 在做这部分实验之前，要熟悉x86的内存管理架构：段机制和页机制
  - 相关内容见《深入理解Linux内核》第2章
  - 6.828和Linux一样，通过把段描述符的Base字段设为0，绕过了段机制（深入理解Linux内核2.3，6.828 lab2 3.1）

### 3.1 虚拟地址、线性地址和物理地址

#### 3.1.1 6.828如何绕过段机制

- 6.828如何绕过段机制
  - 在boot/boot.S中，安装GDT，该表把所有段基址设为0，并把段的上限设为0xffffffff，绕过了段机制
  - 在实验3中，需要与分段进行更多交换才能设置特权级别，但是在这次实验中可以忽略段机制

#### 3.1.2 地址映射的范围

- 在lab 1的part 3，使用了一个简单的页表，使得内核可以在链接地址0xf0100000上运行，而内核的物理地址是0x00100000，该页表仅仅映射了4MB内存
- 这次将映射从虚拟地址0xf0000000开始的前面256MB物理内存，还要映射虚拟地址空间的其它区域

#### 3.1.3 gdb调试下查看物理地址

- gdb调试下，只能通过虚拟地址查看内存存放的内容，需要qemu内置的monitor来访问物理地址，以及其它一些信息

- 在lab目录下输入如下指令，打开monitor

  ```shell
  qemu-system-i386 -hda obj/kern/kernel.img -monitor stdio -gdb tcp::26000 -D qemu.log 
  ```

- 一些常见的指令

  ```shell
  xp/Nx addr # 查看paddr物理地址处开始的，N个字的16进制的表示结果
  inforegister # 查看从paddr物理地址开始的，N个字的16进制的表示结果
  # ...
  ```

- 一旦进入保护模式，就无法直接使用物理地址或线性地址，也就是说C程序中所有指针都是虚拟地址

#### 3.1.4 虚拟地址和物理地址类型的表示

-  uintptr_t表示虚拟地址，physaddr_t表示物理地址
- 都是uint32_t，两个类型的变量相互赋值不会报错，但是如果要解引用（*取值运算）则会报错

- 可以把uintptr_t类型强制转换成Type* 类型，再解析引用，但是不能强制转换physaddr_t，因为physaddr_t在C程序中并不是一个指针，强制转换成一个指针是无意义的

#### 3.1.5 KADDR和PADDR

- KADDR(pa)把物理地址转换成虚拟地址，PADDR(va)把虚拟地址转换成物理地址
  - 直接加减KERNBASE
  - 适用于KERNBASE后面的256MB的虚拟地址
  - 适用于全部物理内存吗？？？如果适用，为什么还需要页表？？？

### 3.2 引用计数

- 在未来的实验中，可能会出现多个虚拟地址（或在多个虚拟地址空间）映射相同的物理页面

  - 通过PageInfo的pp_ref字段，记录对每个物理页面的引用计数

  - 引用计数变为0时，页面可以释放，因为不再使用该页面
  - 一个物理页的引用计数，等于被所有虚拟地址比UTOP小的虚拟页引用的次数
    - UTOP在哪？？？需要看一下虚拟地址空间的结构

- 使用page_alloc函数时应当注意，它返回的PageInfo结构体对应的页的引用计数总是0，因此需要立即+1

### 3.3 页表管理

- 现在，可以实现管理页表的代码了
  - 需要实现kern/pmap.c的以下代码
    - pgdir_walk()
    - boot_map_region()
    - page_lookup()
    - page_remove()
    - page_insert()
  - check_page() 测试页表管理例程

## 4 第三部分：内核地址空间

- JOS把处理器的32位线性地址空间分为两部分
  - 用户环境占据低地址的那部分，即用户地址空间
  - 内核占据高地址的部分，即内核地址空间
  - 分界线是定义在memlayout.h文件中的一个宏 ULIM
  - JOS为内核保留了接近256MB的虚拟地址空间
    - 这就可以理解了，为什么在Lab 1中要给操作系统设计一个高地址（0xf0000000）的地址空间，如果不这样做，用户环境的地址空间就不够了
- 内存布局图参考inc/memlayout.h

### 4.1 权限和故障隔离

- 内核和用户进程只能访问各自的地址空间，
  - 必须在x86页表中使用访问权限位(Permission Bits)来使用户进程的代码只能访问用户地址空间，而不是内核地址空间
  - 否则用户代码中的一些错误可能会覆写内核中的数据，最终导致内核的崩溃。
- 不同权限对地址空间的不同部分的访问
  - 处在用户地址空间中的代码不能访问高于ULIM的地址空间，但内核可以读写这部分空间
  - 内核和用户对于地址范围[UTOP, ULIM]有着相同的访问权限，那就是可以读取但是不可以写入
    - 这一个部分的地址空间通常被用于把一些只读的内核数据结构暴露给用户地址空间的代码
  - UTOP之下的地址范围是给用户进程使用的，用户进程可以访问，修改这部分地址空间的内容

### 4.2 初始化内核地址空间

- 现在我们要设置UTOP之上的地址空间
  - 这也是整个虚拟地址空间中的内核地址空间部分
  - inc/memlayout.h文件中展示了这部分地址空间的布局
  - 使用刚刚编写的函数来设置这些地址的布局
  - 需要通过check_kern_pgdir()和check_page_installed_pgdir()函数的测试
- 实验代码见仓库
- 一些问题和回答
  - ###

### 4.3 地址空间布局的其它情况

- 进程的虚拟地址空间布局不是只有这一种情况
  - 内核也可以映射到低地址处
  - Linux是把虚拟地址 [3GB, 3GB + 896MB) 映射到物理地址 [0, 896MB)
  - 还可以考虑进程的地址空间就是[0, 4GB)，无需为内核预留一部分空间
    - 现在进程的地址空间是[0, 0xeec00000)，也就是UTOP以下

## 5 mem_init函数以及相关代码的注意点

- 下面记录一些实验过程中的一些比较有价值的地方

### 5.1 boot_alloc

- 函数原型

  ```c
  static void* boot_alloc(uint32_t n);
  ```

- 分配大小为n字节的内存

- 仅仅用于虚拟内存空间没构建好的时候使用，以后使用page_alloc

- next_free和nalloc页对齐

### 5.2 page_init

- 函数原型

  ```c
  void page_init();
  ```

- 初始化PageInfo数组
  - 哪些空闲，哪些被占用，见代码注释
- 注意boot_alloc(0)可以定位到nextfree的位置

### 5.3 page_alloc

- 函数原型

  ```c
  struct PageInfo* page_alloc(int alloc_flags);
  ```

- 参数标志是否把页面清零

### 5.4 page_free

- 函数原型

  ```c
  void page_free(struct PageInfo *pp);
  ```

### 5.5 pgdir_walk

- 函数原型

  ```c
  pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create)
  ```

- 得到虚拟地址va对应的页表项指针，create标志在没有对应页表的情况下是否创建页表
- 能用[]运算的不要用+运算，容易出错
- 能进行[]运算的一定是虚拟地址
- 得到页表项，不用再处理页是否存在的情况

### 5.6 boot_map_region

- 函数原型

  ```c
  static void boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
  ```

- 把虚拟地址 [va, va + size) 映射到物理地址[pa, pa + size)，perm决定了页表项的权限位
- 一个页表项包含了PGSIZE大小，因此遍历的时候每次偏移PGSIZE

### 5.7 page_lookup

- 函数原型

  ```c
  struct PageInfo *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
  ```

- 找虚拟地址va对应的PageInfo结构
  - 如果pte_store不为空，就把虚拟地址va对应的页表项存到pte_store
  - 如果找不到va对应的页表项，说明va没有映射到哪个物理页，因此直接返回即可，不用创建页表
- 中断在哪里实现？？？
- 注意区分页表项和页表项指针，搞清楚它们两个为0分别代表什么

### 5.8 page_remove

- 函数原型

  ```c
  void page_remove(pde_t *pgdir, void *va)
  ```

- tlb_invalidate什么作用？？？

### 5.9 page_insert

- 函数原型

  ```c
  int page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
  ```

- 建立虚拟地址va和pp对应的物理地址的映射关系
- 为什么要先引用计数+1，再如果page_remove时-1？？？

### 5.10 一些其它映射

- ###

### 5.11 mem_init

- ###

### 5.12 check_page_free_list

- 这个函数除了测试以外，还调整了空闲链表的顺序，原来是按地址从大到小，现在是从4M开始到0，再到最大，再变小

## 6 内存布局变化

- 回顾一下从BIOS到mem_init完成后，虚拟地址和物理地址布局的变化过程

### 6.1 加载BIOS和boot loader后

![内核加载前](/home/xiaogouguohe/6.828/lab/note/img/内核加载前.png)

### 6.2 加载内核可执行文件后

![加载内核可执行文件后](/home/xiaogouguohe/6.828/lab/note/img/加载内核可执行文件后.png)

### 6.3 初始化页目录和PageInfo数组后

![kern_pgdir和PageInfo数组](/home/xiaogouguohe/6.828/lab/note/img/kern_pgdir和PageInfo数组.png)

- boot_alloc给kern_pgdir分配空间
- 初始化kern_pgdir

### 6.4 建立其它一些位置的映射关系

![mem_init完成](/home/xiaogouguohe/6.828/lab/note/img/mem_init完成.png)



















- 内存相关的流程

  - BIOS和boot loader

    - 物理内存布局
    - 虚拟内存布局，4MB的映射是什么时候建立起来的？？？

  - cons_init，输入输出

  - mem_init

    - i386_detect_memory
    - boot_alloc给kern_pgdir
    - 初始化kern_pgdir
      - 完成这个之后，实际上就可以通过调用那些函数，实现映射关系了
      - 
    - boot_alloc给PageInfo数组
      - 以上都是在4MB的映射当中
      - 物理内存布局
      - 虚拟内存布局
    - page_init，初始化PageInfo数组
    - 调整free_list顺序

    - 建立一些其它映射关系



// 1. page_insert重复计数如何避免，page_ref++放在page_remove前，为什么

// 2. 偏移量要用[]

// 3. 空闲链表的调整

// 4. 页表项指针，虚拟地址

// 5. boot_alloc，end和它的修正

// 6. boot_map_region，为什么每页只需要一次映射?