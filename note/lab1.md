# Lab 1: Booting a PC

## 1.1 简介

- 实验分成三部分
  - 第一部分的重心在于熟悉x86汇编语言，QEMU，和给PC加电时引导程序的步骤
  - 第二部分查看了6.828内核的引导加载程序，它位于实验的boot目录下
  - 第三部分研究了6.828内核本身的初始化模板，它的名字叫JOS，位于kernel目录下

### 1.1.1 启动软件

- ###

### 1.1.2 提交步骤

- ###

## 1.2 第一部分：PC引导过程

- Exercise 1介绍x86汇编语言和PC引导过程，需要QEMU和QEMU / GDB调试
  - 不用写代码

### 1.2.1 x86汇编入门

- 资料：PC Assembly Language Book

- ###

### 1.2.2 模拟x86

- 不在真实的PC上开发操作系统，而是在一个能模拟PC的程序上进行开发
  - 使用QEMU仿真器
  - QEMU可以充当GDB的远程调试目标
- 把Lab 1文件解压到一个目录，然后make，构建将要启动的引导加载程序和内核
  - 会在以后的实验中逐渐充实这个内核
- 命令行和QEMU界面可以等价
- 目前只有两个命令：help和kerninfo
  - help命令的效果很显然，输出目前能使用的命令
  - kerninfo命令打印的内容
    - 将obj / kern / kernel.img的内容复制到真实硬盘的前几个扇区中，将该硬盘插入真实PC中，打开它，然后在上面看到完全相同的内容
    - 不建议这么做，会破坏原来真实PC的扇区

### 1.2.3 PC的物理地址空间

- 接下来，详细介绍PC的启动方式

- PC物理地址空间是硬连线的，具有以下常规布局

  ```
  +------------------+  <- 0xFFFFFFFF (4GB)
  |               32-bit              |
  |  memory mapped   |
  |            devices             |
  |                                         |
  /\/\/\/\/\/\/\/\/\/\/\/\
  
  /\/\/\/\/\/\/\/\/\/\/\/\
  |                                         |
  |             Unused            |
  |                                         |
  +-------------------------+  <- depends on amount of RAM
  |                                         |
  |                                         |
  | Extended Memory  |
  |                                         |
  |                                         |
  +-------------------------+  <- 0x00100000 (1MB)
  |          BIOS ROM          |
  +-------------------------+  <- 0x000F0000 (960KB)
  |      16-bit devices,     |
  |    expansion ROMs   |
  +-------------------------+  <- 0x000C0000 (768KB)
  |        VGA Display         |
  +-------------------------+  <- 0x000A0000 (640KB)
  |                                         |
  |       Low Memory       |
  |                                         |
  +-------------------------+  <- 0x00000000
  ```

  - 第一台PC，处理器是16位的，它只能处理1MB的物理内存
    - 16位原本只能处理256KB的物理内存，后来引入了一种选择机制（见《深入理解Linux内核》）
    - Low Memory那部分是给RAM用的
    - 硬件保留了从0x000A0000到0x000FFFFF的384KB区域，用于特殊用途，例如视频显示缓冲区和非易失性存储器中保存的固件
      - 此保留区中最重要的部分是基本输入/输出系统（BIOS），它占用从0x000F0000到0x000FFFFF的64KB区域
      - 在早期的PC中，BIOS被保存在真正的ROM中
      - 当前的PC将BIOS存储在可更新的闪存中
      - BIOS负责执行基本的系统初始化，例如激活视频卡和检查已安装的内存量。执行此初始化之后，BIOS从某个适当的位置（例如软盘，硬盘，CD-ROM或网络）加载操作系统，并将计算机的控制权传递给操作系统
  - 80286和80386分别支持16MB和4GB物理地址空间，打破了1MB字节的限制
    - 依然位低1MB的物理地址空间保留了原始布局，以便向后兼容现有软件
    - 因此，现代PC在从0x000A0000到0x00100000的物理内存中有个“空洞”，将RAM分为常规内存（前640KB）和扩展内存（其余部分）

  - 最新的x86处理器可以支持超过4GB的物理RAM
    - ###

### 1.2.4 The ROM BOIS

- 本部分的实验，将使用QEMU的调试工具，来研究IA-32兼容计算机的启动方式

- 输出中有一行

  ```bash
  [f000：fff0] 0xffff0: ljmp $0xf000, $0xe05b
  ```

  从输出中可以获得以下结论

  - 第一条指令执行前，PC的值是0xffff0

    - 该地址位于ROM BIOS保留的64KB区域的最顶部

  - 中括号内的是分段地址CS: IP，它们是如何转换成物理地址的

    - PC启动的时候采用实模式寻址的方式，地址转换根据以下公式工作：物理地址 = 16 *段 + 偏移量
    - 在这个例子中，0xf000 * 16 = 0xf0000，再加上0xfff0，得到物理地址0xffff0
    - 为了实现向后兼容性，即原来运行在8088处理器上的软件仍旧能在现代处理器上运行，所以现代的CPU都是在启动时运行于实模式，启动完成后运行于保护模式
      - BIOS就是PC刚启动时运行的软件，所以它必然工作在实模式

  - 要执行的第一条指令是jmp指令，跳转到分段地址0xfe05b

    - 这是很合理的，因为之前的地址离BIOS的末尾已经只有16个字节了，需要跳到早一点的地址，才能正常执行若干指令

  - 自己的BIOS的第一条跳转指令是

    ```assembly
    [f000:fff0]    0xffff0:	ljmp   $0x3630,$0xf000e05b
    ```

    跳转的地址和实验文档的不太一样，可能是因为物理内存布局和实验文档的不太一致，boot loader也不知道被BIOS加载到哪里去了，实验文档说是加载到0x7c00，自己的机器上好像也不是加载到这个地址，因此后面也不太清楚如何对boot loader进行gdb调试

- BIOS运行时，它将建立一个中断描述符表并初始化各种设备，例如VGA显示

- QEMU窗口中看到的“正在启动SeaBIOS”消息的来源

- 初始化PCI总线和BIOS知道的所有重要设备后，它将搜索可引导设备，例如软盘，硬盘驱动器或CD-ROM

  - 最终，BIOS在找到可引导磁盘时，会从磁盘读取引导加载程序，并将控制权转移给该引导加载程序

## 1.3 第二部分：引导加载程序

### 1.3.1 引导扇区

- 引导扇区
  - PC的软盘和硬盘分为512个字节的区域，称为扇区
    - 扇区是磁盘的最小传输粒度
  - 如果磁盘是可引导的，则第一个扇区称为引导扇区，因为这是引导加载程序代码所在的位置
  - 当BIOS找到可引导的软盘或硬盘时，它将512字节的引导扇区加载到物理地址0x7c00至0x7dff的内存中，然后使用jmp指令将CS：IP设置为0000：7c00，将控制权传递给引导程序装载机
    - 像BIOS加载地址一样，这些地址是相当任意的-但它们对于PC是固定的和标准化的
- 现代BIOS从CD-ROM引导的方式更加复杂
  - 扇区大小为2048字节
  - BIOS可以在将控制权转移到磁盘前，将更大的引导映像（不仅仅是一个扇区）从磁盘加载到内存
  - 但是，对于6.828，将使用常规的硬盘启动机制，这意味着我们的启动加载程序必须适合512个字节

### 1.3.2 引导加载程序

- 引导加载程序由一个汇编语言源文件boot / boot.S和一个C源文件boot / main.c组成
- 引导加载程序有两个主要功能
  - 处理器从实模式切换到32位保护模式，
    - 只有在这种模式下，软件才能访问处理器物理地址空间中1MB以上的所有内存
    - PC Assembly Language Book的1.2.7和1.2.8节中简要介绍了保护模式，并且Intel体系结构手册中对此进行了详细介绍
    - 简单来说，在保护模式下将分段地址（段：偏移对）转换为物理地址的情况会有所不同，并且在转换偏移之后为32位而不是16位
      - 段地址如何转换为物理地址，参考《深入理解Linux内核》第2章的内容
      - 简单来说，保护模式下，段机制被绕过了，也就是分段地址等于线性地址，然后线性地址再通过页机制转换为物理地址
  - 引导加载程序通过x86的特殊I / O指令直接访问IDE磁盘设备寄存器，从而从硬盘读取内核
    - 特定I / O指令的含义，请查看6.828参考页上的“ IDE硬盘控制器”部分
    - 在这里不需要学习太多有关对特定设备进行编程的知识：在实践中，编写设备驱动程序是OS开发中非常重要的一部分，但是从概念或体系结构的角度来看，这是非常枯燥的
- 了解引导加载程序的源代码之后，请查看文件obj/boot/boot.asm
  - 该文件是我们的GNUmakefile在编译引导加载程序之后创建的引导加载程序的反汇编，源文件为boot/boot.S和boot/main.c
  - 该反汇编文件方便我们查看所有引导加载程序代码在物理内存中的位置，并且可以跟踪在GDB中逐步调试引导加载程序时发生的情况
  - 同样，obj/kern/kernel.asm包含对JOS内核的反汇编，这对于调试很有用

### 1.3.3 boot/boot.S

- ```assembly
  # Switch from real to protected mode, using a bootstrap GDT
  # and segment translation that makes virtual addresses 
  # identical to their physical addresses, so that the 
  # effective memory map does not change during the switch.
  lgdt    gdtdesc
  ```

  - lgdt指令，把gdtdesc这个标识符送入全局描述符表寄存器gdtr中

    - 回忆《深入理解Linux内核》2.2.2对gdtr的描述

    - 为什么可以这样把gdtdesc送入gdtr？？？

  - gdtdesc是一个标识符，在文件的末尾可以看到它

    ```assembly
    # Bootstrap GDT
    .p2align 2                                # force 4 byte alignment
    gdt:
      SEG_NULL				# null seg
      SEG(STA_X|STA_R, 0x0, 0xffffffff)	# code seg
      SEG(STA_W, 0x0, 0xffffffff)	        # data seg
    
    gdtdesc:
      .word   0x17                            # sizeof(gdt) - 1
      .long   gdt                             # address gdt
    ```

    - 可以看到，gdtdesc的低16位是gdt表的长度-1，高32位是标识符的地址，还是标识符的值？？？

  - gdt是个标识符，gdt包含3个表项，分别是3个段描述符

    - 段描述符见深入理解Linux内核2.2.2

    - xv6没有使用分段机制，也就是说数据和代码是写在一起的，所以数据段和代码段的起始地址都是0x0，大小都是0xffffffff = 4GB

      - 没有段机制，数据和代码就一定会写在一起吗？？？Linux也绕过了段机制

    - 调用SEG()构造GDT表项，这个子函数定义在mmu.h中

      ```c
      #define SEG(type,base,lim)                    \
                          .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);    \
                          .byte (((base) >> 16) & 0xff), (0x90 | (type)),        \
                          (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)
      ```

      - 需要三个参数，type是这个段的访问权限，base是这个段的起始地址，lim是这个段的大小界限

- 回到刚才那里

  ```assembly
  movl    %cr0, %eax
  orl     $CR0_PE_ON, %eax
  movl    %eax, %cr0
  ```

  - 后面3个操作是在修改CR0寄存器的值
    - CR0~CR3寄存器都是80x86的控制寄存器
    - 对CR0作用的描述见深入理解Linux内核2.4
  - $CR0_PE的值定义于"mmu.h"文件中，为0x00000001，因此上面的操作是把CR0寄存器的bit0置1，这是保护模式启动位，置1代表保护模式启动

- ```assembly
  # Jump to next instruction, but in 32-bit code segment.
  # Switches processor into 32-bit mode.
  ljmp    $PROT_MODE_CSEG, $protcseg
  ```

  跳转指令，目的在于把当前模式切换成32位地址模式

- ```assembly
  .code32                     # Assemble for 32-bit mode
  protcseg:
    # Set up the protected-mode data segment registers
    movw    $PROT_MODE_DSEG, %ax    # Our data segment selector
    movw    %ax, %ds                # -> DS: Data Segment
    movw    %ax, %es                # -> ES: Extra Segment
    movw    %ax, %fs                # -> FS
    movw    %ax, %gs                # -> GS
    movw    %ax, %ss                # -> SS: Stack Segment
  ```

  修改这些寄存器的值

  - 这些寄存器都是段寄存器
  - 刚刚加载完GDTR寄存器，就必须重新加载所有段寄存器的值，其中CS段寄存器必须通过长跳转指令进行加载？？？

- ```assembly
  # Set up the stack pointer and call into C.
  movl    $start, %esp
  call bootmain
  ```

  设置当前%esp寄存器（栈指针）的值，准备跳转到main.c文件中的bootmain函数处，接下来分析bootmain函数

### 1.3.4 boot/main.c

- ```c
  // read 1st page off disk
  readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);
  ```

  - readseg的定义在main.c的后面

    ```c
    // Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
    // Might copy more than asked
    void
    readseg(uint32_t pa, uint32_t count, uint32_t offset)
    ```

    从内核起始地址开始，先偏移offset，然后从这里开始读取count字节大小的内容，读到物理地址pa处

  - 因此这行代码相当于把内核的第一个页读取到内存地址ELFHDR处

    - ELFHDR为0x10000
    - SECTSIZE是块大小，为512，SECTSIZE*8 = 4096
    - 事实上完成这些相当于把操作系统映像文件的elf头部读取出来放入内存中

  - 关于elf文件，见csapp第7章（其实并不是很了解）

- ```c
  if (ELFHDR->e_magic != ELF_MAGIC)
  	goto bad;
  ```

  - 判断elf头部的e_magic字段的值是否为ELF_MAGIC，也就是这是否为一个合法的elf文件

- ```c
  // load each program segment (ignores ph flags)
  ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
  ```

  ph是Program Header Table的起始地址

  - 这个表存放了所有段的信息，通过这个表才能找到代码段、数据段等等
  - ELFHDR是elf表头的起始地址，而e_phoff是Program Header Table相对于elf表头的偏移量，因此两者相加得到Program Header Table的起始地址

- ```c
  eph = ph + ELFHDR->e_phnum;
  ```

  eph是Program Header Table的末尾地址

  - e_phnum是Program Header Table表项的个数，也就是段的个数
  - 注意ph的类型，指针运算

- ```c
  for (; ph < eph; ph++)
      // p_pa is the load address of this segment (as well
      // as the physical address)
      readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
  ```

  这个循环是把所有的段都加载到内存中（1.3.7）

- ```c
  // call the entry point from the ELF header
  // note: does not return!
  ((void (*)(void)) (ELFHDR->e_entry))();
  ```

   e_entry指向这个ELF文件的执行入口地址，相当于从这里开始运行这个内核文件，字词控制权从boot loader转交到内核

### 1.3.5 一些调试技巧

- gdb调试时，输入x/30i 0x7c00，可以打印出从地址0x7c00开始的30条汇编指令

### 1.3.6 对实验文档提出的一些问题的回答

1. 处理器从哪个位置开始运行于32位模式？是什么导致了从16位模式切换到32位模式？

   答：在boot.S文件中，计算机首先工作于实模式，此时是16bit工作模式；当运行完 " ljmp $PROT_MODE_CSEG, $protcseg " 语句后，正式进入32位工作模式

2. boot loader中执行的最后一条语句是什么？内核被加载到内存后执行的第一条语句又是什么？

   答：boot loader执行的最后一条语句是bootmain子程序中的最后一条语句 " ((void (*)(void)) (ELFHDR->e_entry))(); "，即跳转到操作系统内核程序的起始指令处

   这个第一条指令位于/kern/entry.S文件中，第一句 movw $0x1234, 0x472

3. 内核的第一条指令在哪里？

   答：上一个问题中已经回答过这个问题，第一条指令位于/kern/entry.S文件中

4. boot loader是如何知道它要读取多少个扇区才能把整个内核都送入内存的呢？在哪里找到这些信息？

   - Program Header Table中的每个表项对应于操作系统中的一个段，包含了段大小，段地址偏移等信息，所以找到这个表，就可以知道每个段在哪里，以及占据了多少个扇区
   - Program Header Table存放在内核映像文件的ELF头部里

### 1.3.7 加载内核

- 本节将在boot/main.c中进一步详细介绍引导加载程序的C语言部分

- 为了搞清楚boot/main.c的意义，需要了解一下ELF文件

  - csapp第7章，其中ELF文件的大致格式见7.4的图7-3，在这里我们主要关注以下节
    - .text: 程序的可执行指令
    - .rodata: 只读数据，例如字符串常量
    - .data: 程序的初始化数据，如int x = 5;这样声明的全局变量
      - 怎么存储的？？？
    - .bss: 程序的未初始化变量
      - 事实上是为了这些未初始化变量预留空间
      - 因此无需在ELF文件中存储.bss的内容，而是通过链接器仅仅记录.bss节的地址和大小
      - 加载程序或程序本身必须将.bss清零

- 通过下面的指令来考察JOS内核中所有段的名字，大小和地址

  ```bash
  objdump -h obj/kern/kernel
  ```

  得到的结果如图：

  ![image-20210417152414749](/home/xiaogouguohe/.config/Typora/typora-user-images/image-20210417152414749.png)

  - 除了之前提到的四个段，还有一些其他段
  - 两个比较重要的字段：VMA（链接地址）和LMA（加载地址）
    - 加载地址代表这个段被加载到物理内存中，它所在的物理地址
    - 链接地址代表这个段希望被存放到的逻辑地址
    - 为什么要区分这两个？？？
    - 通常这两个都是一样的

- 如1.3.4，每个ELF文件都有一个Program Headers Table，指明了ELF文件中哪些段被加载到内存中的哪个地址，可以通过下面的指令来获取kernel的Program Headers Table的信息

  ```shell
  objdump -x obj/kern/kernel
  ```

  得到的结果如图

  ![2](/home/xiaogouguohe/6.828/lab/note/img/2.png)

  - 后面还有一些信息，比如所有节（段）的信息，如上一张图，还有符号表
  - 通过两张图的对比，可见有些段没有被加载到内存
  - 需要被加载到内存中的段被标记为LOAD

### 1.3.8 boot loader和内核的链接地址和加载地址

- boot loader运行时，没有任何分段或分页机制，因此boot loader可执行文件的链接地址和加载地址是一样的

  - 这里的BIOS默认把boot loader加载到0x7c00内存地址处，所以要求boot loader的链接地址也要在0x7C00
  - boot loader链接地址的设定在boot/Makefrag中完成的，所以需要改动这个文件的值
  - ###（Exercises 5的内容）

- 回顾内核的加载地址和链接地址（1.3.7）

  - 和boot loader不一样，内核的所有段的这两个地址都是不一样的，如1.3.7

  - 在后面会看这个问题

  - 在ELF头部中，除了Program Headers Table用来保存各个段的大小、起始地址等信息之外，还有一个e_entry字段，存放了内核可执行程序的执行入口处的链接地址

    - 注意区分内核可执行文件的加载地址、链接地址，和内核的每个段的加载地址、链接地址，是不一样的吗？？？

    - 通过以下指令可以查看内核程序的链接地址

      ```shell
      objdump -f obj/kern/kernel 
      ```

      得到的结果如下：

      ![3](/home/xiaogouguohe/6.828/lab/note/img/3.png)

      内核可执行文件的链接地址为0x0010000c，这个地址大概所在的位置见1.2.3

- Exercises 6

  - 这样查到的地址是物理地址还是虚拟地址？？？

## 1.4 第三部分：内核

- 从这里开始更详细地研究JOS内核，并且还会写一些代码
- 和boot loader一样，内核从一些汇编语言代码开始，它们设置一些东西，来保证C程序正确执行

### 1.4.1 虚拟内存

- 在运行boot loader时，boot loader的链接地址（虚拟地址）和加载地址（物理地址）是一样的；但是当进入到内核程序后，这两种地址就不同了（1.3.8）

  - 内核程序会被链接到一个高的虚拟地址空间处，比如0xf0100000，目的是让处理器的虚拟地址空间的低地址部分能够被用户利用来进行编程
  - 但许多机器并没有那么大的物理内存，所以不能把内核的0xf0100000虚拟地址映射到物理地址0xf0100000的存储单元处
  - 解决方案是在虚拟地址空间中，还是把内核放在高地址处0xf0100000，但在实际的内存中我们把内核放在一个低的物理地址空间处，如0x00100000
    - 当用户程序想访问一个内核的指令时，首先给出的是一个高的虚拟地址，然后通过分段、分页机制把这个虚拟地址映射为物理地址
    - 在这个实验当中，没有采取计算机常用的分页机制，而是用一个程序lab/kern/entrygdir.c进行映射，只映射了这两部分
      - 虚拟地址范围：0x00000000~0x00400000，映射到物理地址范围：0x00000000~0x00400000
      - 虚拟地址范围：0xf0000000~0xf0400000，映射到物理地址范围：0x00000000~0x00400000
      - 任何不在这两个范围的地址都会引起硬件异常
      - 这里只映射了前4MB的物理内存，已经足够启动程序的时候使用了，而在下一个实验中，将映射PC的整个底部的物理地址，也就是虚拟地址范围：0xf0000000

- 建立虚拟地址到物理地址映射的时机

  - 哪里实现了物理地址 = 虚拟地址 - KERNBASE？？？

  - 这里的映射是指上文提到的entrygdir.c的手动映射，还是分页机制的映射？？？

  - 在kern/entry.S设置CR0_PG标志，内存引用才被视为虚拟地址，虚拟内存硬件会将这些引用转换为物理地址；而在此之前，内存引用都被视为物理地址

    - 为什么entry_pgdir可以实现虚拟地址到物理地址的映射，entrypgdir.c看不明白？？？

    - 给定一个虚拟地址0xf010002f，又是如何根据cr3还有其它，得到物理地址0x0010002f的？？？

- 对entry.S代码的分析

  - 

- 通过Exercises 7来理解地址映射的时机

  - 打开一个终端，切换到lab目录下，输入make qemu-gdb

  - 再打开一个终端，切换到lab目录下，输入make gdb

  - 这个时候去检查地址0x00100000和0xf0100000的值

    ![image-20210417225608530](/home/xiaogouguohe/.config/Typora/typora-user-images/image-20210417225608530.png)

  - 在内核入口处设置断点，运行到断点，然后单步执行到

    ```assembly
    mov %eax, %cr0
    ```

    之前，这时再去检查地址0x00100000和0xf0100000的值

    ![image-20210417225715846](/home/xiaogouguohe/.config/Typora/typora-user-images/image-20210417225715846.png)

    - 此时还没有建立地址映射，地址引用都是对物理地址的引用
    - 物理地址0x00100000应该是存放了内核的某个段，所以值不为零
    - 物理地址0xf0100000还没用到，因此值为零

  - 执行

    ```assembly
    mov %eax, %cr0
    ```

    再去检查地址0x00100000和0xf0100000的值![6](/home/xiaogouguohe/6.828/lab/note/img/6.png)

    - 此时已经建立地址映射，地址引用都是对虚拟地址的引用
    - 参考上述的映射关系，这两个虚拟地址的引用都会映射到物理地址0x00100000上
    - 为什么设置%cr0就可以建立映射？？？

  - 还有一问是，如果不建立正常的地址映射，会怎样导致无法正常工作？需要在kern/entry.S中注释掉上述的mov指令，这个问题先跳过

### 1.4.2 参数个数可变的函数

- 在理解标准化输出到控制台之前，需要搞清楚参数可变的函数如何实现
  
  - C语言库的printf就是参数个数可变的函数
- 相关的函数和数据结构
  - va_list
  - va_start
  - va_arg
  - va_end

- 下面通过一个例子来分析

  ```c
  #include <stdio.h>
  #include <stdarg.h>
  #include <stdlib.h>
  
  //第一个参数指定了参数的个数
  int sum(int number,...)
  {
      va_list vaptr;
      int i;
      int sum = 0;
      va_start(vaptr, number);
      for(i = 0; i < number; i++) {
          sum += va_arg(vaptr, int);
      }
      va_end(vaptr);
      return sum;
  }
   
  int main() {
      printf("%d\n",sum(4,4,3,2,1));
      system("pause");
      return 0;
  }
  ```

#### 1.4.2.1 va_list

- va_list的定义如下

  ```c
  typedef char *  va_list;
  ```

- 在可变参数的函数中，一般用来存储可变参数，相当于数组的首地址
  
  - 在上面的例子中，main函数调用sum函数，其中第一个4是number参数，{4, 3, 2, 1}是可变参数列表，va_list就是这个列表

#### 1.4.2.2 va_begin

- 宏定义，原型是

  ```c
  #define va_start _crt_va_start
  #define _crt_va_start(ap, v) ( ap = (va_list)_ADDRESSOF(v) + _INTSIZEOF(v) )
  ```

- 这个宏定义的作用是，把参数列表ap指向当前函数的参数列表
  
  - 在例子当中，v相当于number，而vaptr指向了参数列表{4, 3, 2, 1}
- 为什么这样的宏定义可以实现把ap指向参数列表
  - v的地址加上v的大小，得到的就是参数列表的第一个参数
  - 因为函数的参数都是从右往左逐个压入寄存器或栈的，所以虚拟地址都是相邻的
  - 栈是向低地址方向增长的，而最右的参数最先入栈，因此参数从左到右，地址不断增大
- 宏定义\_ADDRESSOF(v)是对v取地址，而\_INTSIZEOF(v)是计算v的大小，并且按照int的字节大小对齐，也就是如果v的大小是5字节，得出来的结果应该是8（见1.4.2.3）

#### 1.4.2.3 _INTSIZEOF(n)实现内存对齐

- 假设现在要实现5字节对齐，有个对象的大小是n字节，那么对齐之后的大小是floor((n + 5 - 1) / 5) * 5

  - 当n为9的时候，得到的结果为10
  - 当n为10的时候，得到的结果为10
  - 在C语言可以写成(n + 5 - 1) / 5 * 5

- 现在是按照int类型的大小对齐，而int类型的大小是2的幂

  - 除以2<sup>m</sup> 相当于右移m位，乘以2<sup>m</sup> 相当于左移m位

  - 因此先除以2<sup>m</sup> ，再乘以2<sup>m</sup> ，相当于把低m位清零

    ```c
    #define _INTSIZEOF(n) ( (sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1) )
    ```

#### 1.4.2.4 va_arg

- 宏定义的原型

  ```c
  #define va_arg _crt_va_arg
  #define _crt_va_arg(ap, t) ( *(t *)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)) )
  ```

- 作用
  - 返回当前的参数，且保证是它原来的类型
  - ap指向下一个参数

#### 1.4.2.5 va_end

- 宏定义的原型

  ```c
  #define va_end _crt_va_end
  #define va_end(ap) (ap = (va_list)0)
  ```

- 作用
  
  - 把指针ap作废

### 1.4.3 标准化输出到控制台

- 很多时候printf()之类的功能被视为理所当然，但是在OS内核中，必须自己实现所有I/O
- 通读kern/printf.c, lib/printfmt.c 和 kern/console.c，了解它们之间的关系
  - 在以后的实验中会解释为什么printfmt.c位于单独的lib目录
  - 相关函数的一些调用关系
    - kern/printf.c的cprintf -> kern/printf/c的vcprintf-> lib/printfmt.c的vprintfmt
    - kern/printf.c的putch -> kern/console.c的cputchar
    - lib/printfmt.c的某些程序 -> kern/console.c的cputchar
  - 接下来几节介绍一些关键的函数

#### 1.4.3.1 kern/console.c

- 文件定义了如何把一个字符显示到控制台上，包括很多对I/O端口的操作

- cputchar实现输出一个字符到控制台

  ```c
  // `High'-level console I/O.  Used by readline and cprintf.
  void
  cputchar(int c)
  {
      cons_putc(c);
  }
  
  // output a character to the console
  static void
  cons_putc(int c)
  {
      serial_putc(c);
      lpt_putc(c);
      cga_putc(c);
  }
  ```

  - cons_putc调用的三个函数暂时略过

#### 1.4.3.2 lib/printfmt.c

- 文件开头的注释提到，这个文件是打印各种样式的字符串的子程序，它们经常被printf，sprintf，fprintf函数所调用，这些代码是同时被内核和用户程序所使用的

- 重点关注vprintfmt

  ```c
  void
  vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap)
  ```

  - 四个输入参数

    - void (\*putch)(int, void\*)

      - 在实际的使用中，putch函数指针被printf.c的putch实现，它调用了kern/console.c的cputchar

      - 它实现的功能是，把第一个参数字符存放到第二个参数代表的位置

      - 事实上，第二个参数是要放入的内存位置的指针，例如想把一个字符值为0x30的字符('0')输出到地址0x01处，程序应该如下

        ```c
        int addr = 0x01; 
        int ch = 0x30;
        putch(ch, &addr);
        ```

      - 这样做的原因是，为了能够把0x30存放到地址0x01后，地址addr加1变成0x02
        
      - 为什么不能传别名？？？
  
  - void *putdat
  
      - 输入的字符要存放的内存地址的指针，也就是第一个参数函数指针的第二个参数
    - 因为是遍历字符串，所以putdat也会随着字符串的遍历而移动
  
  - const char* fmt
  
      ```c
      printf("These are %d test and %d test", n, m)
    ```
  
    中的第一个参数
  
  - va_list ap
  
      ```c
      printf("These are %d test and %d test", n, m)
    ```
  
    中的可变参数列表
  
  - 更多代码细节见代码注释

#### 1.4.3.3 kern/printf.c

- 在搞清楚了前面的基础上，实现理解起来比较简单

#### 1.4.3.4 补充输出格式为八进制的情况

- 见代码

#### 1.4.3.5 一些问题和回答

1. 解释一下printf.c和console.c之间的关系。console.c对外提供了哪些函数？这些函数是如何被printf.c使用的？

   - 它们之间的关系见1.4.3.1~1.4.3.3
   - 对外提供的这些函数，除了static限定的以外，都可以被printf.c使用

2. 解释一下console.c文件中，下面这段代码的含义

   ```c
   // What is the purpose of this?
   if (crt_pos >= CRT_SIZE) {
       int i;
   
       memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
       for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
           crt_buf[i] = 0x0700 | ' ';
       crt_pos -= CRT_COLS;
   }
   ```

   - 字符超出了缓冲区，舍弃掉第0行

   - 留意代码注释，console.c的cga_putc函数，还有对crt_buf和crt_pos的解释

3. 观察下面的一串代码：

   ```c
   int x = 1, y = 3, z = 4;
   cprintf("x %d, y %x, z %d\n", x, y, z);
   ```

   回答下列问题：当调用cprintf时，fmt指向的是什么内容，ap指向的是什么内容；

   按照执行的顺序列出所有对cons_putc, va_arg，和vcprintf的调用。对于cons_putc，列出它所有的输入参数。对于va_arg列出ap在执行完这个函数后的和执行之前的变化。对于vcprintf列出它的两个输入参数的值。

   - ###

4. 运行下面的代码：

   ```c
   unsigned int i = 0x00646c72;
   cprintf("H%x Wo%s", 57616, &i);
   ```

   输出是什么？解释一下为什么是这样的输出？

   - 在kern/monitor.c文件的monitor函数插入这段代码

     ```c
     void
     monitor(struct Trapframe *tf)
     {
     	// ...
     	
     	unsigned int i = 0x00646c72;
     	cprintf("H%x Wo%s", 57616, &i);
     
     	// ...
     }
     ```

     

     重新编译内核，然后运行make qemu，就会打印出结果

     ![7](/home/xiaogouguohe/6.828/lab/note/img/7.png)

     屏幕上出现He110 World

     为什么会输出这个字符串###

   - monitor函数如何被调用

     - entry.S->0386_init->monitor
     - entry.S是内核代码的入口
     - 0386_init是C程序的入口，进行一些初始化工作，然后调用monitor
     - monitor通过死循环，处理屏幕上的输入输出

     - 更多细节见注释

5. 看下面的代码，在'y='后面会输出什么？为什么会这样？

   ```
    cprintf("x=%d y=%d", 3);
   ```

   - 会输出一个不确定的值，因为y没有被指定

6. 假设现在函数参数的入栈顺序是从左到右，应该如何更改cprintf或其接口，以便仍然可以向其传递可变数量的参数？

   - ###

### 1.4.4 栈

- 这一部分探讨C语言如何在x86上使用堆栈，并且还会重新编写一个kernel monitor子程序，记录堆栈的变化轨迹

  - 轨迹是由一系列被保存到堆栈的IP寄存器的值组成的
  - 之所以会产生这一系列被保存的IP寄存器的值，是因为执行了一个程序，程序中包括一系列嵌套的call指令
  - 为何记录段的偏移量？？？

- 操作系统内核是从哪条指令开始初始化它的堆栈空间的，以及这个堆栈坐落在内存的哪个地方

  - 要找到是何时初始化堆栈空间的，必须找到在哪里对%esp的内容进行修改

  - 前面已经分析过，BIOS运行完成后，执行boot.S和main.c文件，它们属于boot loader而不是内核，main.c文件中的bootmain运行到最后时，跳转到entry.S文件中的entry地址处，控制权移交给entry.S，在此之前没有对%esp内容的修改，因此在加载内核之前没有初始化堆栈空间

  - 在entry.S中，调用C语言函数i386_init之前，改变了%esp, %ebp

    ```assembly
    # Clear the frame pointer register (EBP)
    # so that once we get into debugging C code,
    # stack backtraces will be terminated properly.
    movl	$0x0,%ebp			# nuke frame pointer
    
    # Set the stack pointer
    movl	$(bootstacktop),%esp
    
    # now to C code
    call	i386_init
    ```
    说明是在这里初始化内核的堆栈

    - 回顾1.4.1，已经分析过entry.S的部分代码，在这段代码之前，实现的功能是，建立虚拟地址到物理地址的4MB映射

  - 堆栈在内存中的位置和大小

    - 栈顶为bootstacktop，值为0xf0110000
    - 大小为KSTKSIZE，值为8 * PGSIZE = 8 * 4096B = 32KB
    - 因此堆栈实际在内存的0x00108000~0x00110000物理地址空间中
    - bootstacktop的值在哪里定义？？？

- 通过阅读和调试test_backtrace，理解在x86上的C程序调用过程的细节

  - test_backtrace的C语言定义在kern/init.c

    - 递归调用
    - 在init.c的i386_init中对这个函数调用深度为5

  - test_backtrace的反汇编代码在obj/kern/kernel.asm

    - 首先是在过程调用开始时的一些通用操作

      ```assembly
      # 在此之前应该还有以下操作
      # push x # 参数x入栈，不应该是写入寄存器吗？？？
      # call test_backtrace # 返回地址，也就是test_backtrace的下一条指令的地址入栈
      # %ebp作为分隔子过程和父过程的栈帧的分界，现在入栈的是父过程的栈帧底部
      f0100040:	55                   	push   %ebp
      # %ebp的值更新为子过程的栈帧底部
      f0100041:	89 e5                	mov    %esp,%ebp
      f0100043:	56                   	push   %esi
      f0100044:	53                   	push   %ebx
      ```

      - 在运行test_backtrace(5)之前，%esp, %ebp的值分别为0xf010ffe0, 0xf010fff8

  - 通过gdb调试test_backtrace的代码，理解%ebp的作用

    - 找到test_backtrace的父过程，在kernel.asm的i386_init

      ```assembly
      f01000ef:	e8 4c ff ff ff       	call   f0100040 <test_backtrace>
      ```

      因此在0xf01000ef处打上断点，运行到此处，查看%esp, %ebp的值

      ![image-20210421120148980](/home/xiaogouguohe/.config/Typora/typora-user-images/image-20210421120148980.png)

    - 执行call test_backtrace，会把test_backtrace的返回地址，压入栈中

      ![10](/home/xiaogouguohe/6.828/lab/note/img/10.png)

    -  执行push %ebp，%ebp作为分隔子过程和父过程的栈帧的分界，现在入栈的是父过程的栈帧底部

      ![11](/home/xiaogouguohe/6.828/lab/note/img/11.png)

    - 执行mov %esp, %ebp，%ebp的值现在是子过程的栈帧的底部

      ![12](/home/xiaogouguohe/6.828/lab/note/img/12.png)

    - 执行push %esi

      ![13](/home/xiaogouguohe/6.828/lab/note/img/13.png)

    - 执行push %ebx

      ![14](/home/xiaogouguohe/6.828/lab/note/img/14.png)

    - 后面的内容省略

    - 参数x什么时候入栈的？？？

    - 参数x理论上来说是写到寄存器的，在参数小于6个的时候，那么还需要入栈吗，不应该是在调用子过程的时候才入栈保护？？？

    - 为什么要把%esi和%ebx的内容入栈？？？

    - 为什么要调用get_pc_thunk？？？

    - 根据%ebp的链，就可以找到每个过程的栈帧边界

- 实现kern/monitor.c的mon_backtrace函数，来监控每层过程调用时，%ebp, %eip以及参数的值（Exercises 11）
  
  - 见代码和注释
- Exercises 12先跳过