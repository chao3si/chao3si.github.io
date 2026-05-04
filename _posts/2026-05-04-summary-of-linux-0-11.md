---
title: Summary of Linux-0.11
description: My Learning Experience.
author: chao3si
date: 2026-05-04 19:25:00 +0800
categories: [Operating System, Linux Kernel]
tags: [Linux-0.11, OS, Kernel, File System, Memory Management, Process]
pin: true
math: true
mermaid: true
image:
  path: /assets/img/linux-summary/image.png
  lqip: data:image/webp;base64,UklGRpoAAABXRUJQVlA4WAoAAAAQAAAADwAABwAAQUxQSDIAAAARL0AmbZurmr57yyIiqE8oiG0bejIYEQTgqiDA9vqnsUSI6H+oAERp2HZ65qa/VIAWAFZQOCBCAAAA8AEAnQEqEAAIAAVAfCWkAALp8sF8rgRgAP7o9FDvMCkMde9PK7euH5M1m6VWoDXf2FkP3BqV0ZYbO6NA/VFIAAAB
  alt: linux-0.11 on Bochs
---

## 目录
- [一切的开端-从实模式到32位保护模式](#一切的开端-从实模式到32位保护模式)
  - [加载并执行bootsects](#加载并执行bootsects)
  - [执行setup.s](#执行setups)
  - [执行head.s](#执行heads)
- [从main到内核模块组成及分析](#从main到内核模块组成及分析)
  - [内核的一系列初始化](#内核的一系列初始化)
  - [进程切换原理](#进程切换原理)
  - [缓冲区分配算法](#缓冲区分配算法)
  - [底层文件系统算法](#底层文件系统算法)
  - [文件系统系统调用](#文件系统系统调用)
  - [进程控制](#进程控制)
  - [分页变换机制](#分页变换机制)
  - [磁盘底层读写路径](#磁盘底层读写路径)
  - [字符设备](#字符设备)
  - [select实现](#select实现)
  - [signal](#signal)
- [梳理内核各类执行流](#梳理内核各类执行流)
  - [init进程](#init进程)
  - [用户进程与内存管理](#用户进程与内存管理)
  - [多个进程"同时"操作一个文件](#多个进程同时操作一个文件)

## 参考书籍
- Linux内核设计的艺术 图解Linux操作系统架构设计与实现原理
- Linux源码趣读
- UNIX操作系统设计(Maurice J. Bach)
- Linux内核完全注释V5.0

## 寄存器
部分通用寄存器/指针  

| 缩写       | 全称                  | 作用                 |
| :--------------------------- | :--------------- | ------: |
| IP / EIP | Instruction Pointer | 指令指针(下一条指令偏移)      |
| SP / ESP | Stack Pointer       | 栈顶指针               |
| SI / ESI | Source Index        | 字符串/内存拷贝源索引        |
| DI / EDI | Destination Index   | 字符串/内存拷贝目的索引       |
| AX / EAX | Accumulator         | 累加寄存器              |
| CX / ECX | Count               | 计数寄存器(rep/movs 常用) |
| CR3 | Control Register 3               | 页目录基址寄存器 |

部分段寄存器

| 缩写      | 全称              | 作用                      |
| :--------------------------- | :--------------- | ------: |
| CS      | Code Segment    | 代码段选择子(配合 EIP 取指)       |
| DS      | Data Segment    | 默认数据段                   |
| ES      | Extra Segment   | 字符串指令常用目的段              |
| FS / GS | (Extra) Segment | 额外段(386+ 常用于 TLS/内核用途等) |
| SS      | Stack Segment   | 栈段(配合 ESP)              |

## 一切的开端-从实模式到32位保护模式
### 加载并执行bootsect.s
上电BIOS通过int 0x19中断服务程序将bootsect.s载入0x07c00内存  
而这第一批代码的作用就是将软盘的第二批setup.s, 第三批system载入内存  

```
entry start
start: 
  mov  ax,#BOOTSEG
  mov  ds,ax
  mov  ax,#INITSEG
  mov  es,ax
  mov  cx,#256
  sub  si,si
  sub  di,di
  rep
  movw
  jmpi  go,INITSEG
```
首先进行一个移动, 因为目前载入的内存位置未来会被system占据  
源地址 ds:si(0x07c0 << 4 + si) 移动到 es:di(0x9000 << 4 + di) movw 拷贝2字节  
把0x07c00的一个扇区(512B)移动到0x90000地址  

```
  jmpi  go,INITSEG
go:  mov  ax,cs
```
根据cs:ip的代码地址跳转到0x90000之后的go标签继续执行  

```
go:  mov  ax,cs
    mov  ds,ax
    mov  es,ax
  ; put stack at 0x9ff00.
    mov  ss,ax
    mov  sp,#0xFF00    ; arbitrary value >>512
```
jmpi源跳转自动修改cs, 将后续ds/es/ss都设置成cs(0x9000), 顺便设置栈(0x9FF00)为后续复杂软盘读取做准备  

```
load_setup:
  mov  dx,#0x0000    ; drive 0, head 0
  mov  cx,#0x0002    ; sector 2, track 0
  mov  bx,#0x0200    ; address = 512, in INITSEG
  mov  ax,#0x0200+SETUPLEN  ; service 2, nr of sectors
  int  0x13      ; read it
  jnc  ok_load_setup    ; ok - continue
```
给BIOS int 0x13服务程序传参, 从软盘第二扇区载入setup.s到0x90200  
由与内核代码较大所以需要读取各个盘面、磁道、扇区的代码, 后续会有一堆代码来载入system, 这部分没必要细看  

```
  jmpi  0,SETUPSEG
```
<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> bootesct.s的使命完成了, 后续由setup.s接手  
{: .prompt-info }
<!-- markdownlint-restore -->

### 执行setup.s
首先会读取一系列运行所需的机器数据, 如硬盘0,1信息、光标信息、VGA信息等, 后续main函数会用到这些  

```
  cli      ; no interrupts allowed ;
```
改中断向量要关中断  

```
  mov  ax,#0x0000
  cld      ; 'direction'=0, movs moves forward
do_move:
  mov  es,ax    ; destination segment
  add  ax,#0x1000
  cmp  ax,#0x9000
  jz  end_move
  mov  ds,ax    ; source segment
  sub  di,di
  sub  si,si
  mov   cx,#0x8000
  rep
  movsw
  jmp  do_move
```
将ds:si(0x10000)复制到es:di(0x00000), 可以废除BIOS中断向量表、回收完成使命的bootsect.s的代码空间、把内核移至0x00000, 一举多得  

```
end_move:
  mov  ax,#SETUPSEG  ; right, forgot this at first. didn't work :-)
  mov  ds,ax
  lidt  idt_48    ; load idt with 0,0
  lgdt  gdt_48    ; load gdt with whatever appropriate
```
后续会建立全局描述符表, GDT基地址寄存器, 中断描述符表, IDT基地址寄存器  
为什么在setup.s需要建立gdt, 因为后续会打开保护模式  

```
mov  ax,#0x0001  ; protected mode (PE) bit
lmsw  ax    ; This is it;
jmpi  0,8    ; jmp offset 0 of segment 8 (cs)
```
现将中断控制器(PIC)重新编程,让IRQ 0x00~0x0F的中断对应 int 0x20~0x2F(因为int 0x00~0x0F被intel占用了), 随后置位CR0寄存器PE进入保护模式  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 当一个段选择符被加载到一个段寄存器可见部分中时,处理器也同时把段选择符指向的段描述符中的段地址、段限长以及访问控制信息加载到段寄存器的隐藏部分中
{: .prompt-info }
<!-- markdownlint-restore -->

```
gdt:
  .word  0,0,0,0    ; dummy

  .word  0x07FF    ; 8Mb - limit=2047 (2048*4096=8Mb)
  .word  0x0000    ; base address=0
  .word  0x9A00    ; code read/exec
  .word  0x00C0    ; granularity=4096, 386
```
这里的8以二进制理解为0b1000(段选择子), 可以分析出时GDT的第一项, 即跳转到内核代码段中偏移量0的代码  
之前把system移到了0x00000, 所以也就跳转到了head.s了, setup.s完成了使命  

### 执行head.s
head.s处于system的最前面25KB+184B, 有意思的一个点是它自己执行完就会废弃自己, 变成对内核有用的其他东西  
_pg_dir:这个标号后续有伏笔, 是0x00000代表内核起始地址  

```
startup_32:
  movl $0x10,%eax
  mov %ax,%ds
  mov %ax,%es
  mov %ax,%fs
  mov %ax,%gs
  lss _stack_start,%esp
```

```c
struct {
  long * a;
  short b;
  } stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
```
setup.s进入保护模式的远跳转指令已经让cs被设置为了内核代码段. 上边再重新设置所有数据段寄存器ds/es/fs/gs, 设置内核栈_stack_start  

```
  lea ignore_int,%edx
  movl $0x00080000,%eax
  movw %dx,%ax    /* selector = 0x0008 = cs */
  movw $0x8E00,%dx  /* interrupt gate - dpl=0, present */

  lea _idt,%edi
  mov $256,%ecx
rp_sidt:
  movl %eax,(%edi)
  movl %edx,4(%edi)
  addl $8,%edi
  dec %ecx
  jne rp_sidt
  lidt idt_descr
  ret
```
上述代码构建中断描述符表: %edx载入空的中断服务程序地址, %eax载入(0x0008[段选择符]0000). 把%edx的低16bit(%dx)赋给%eax的低16bit(%ax), 用完了%dx就覆盖为0x8E00. 这些操作主要是构建出了8字节的中断描述符, 低4字节是%eax, 高4字节是%edx  
然后复制256次, 这与bootsect.s的操作类似的  

```
setup_gdt:
  lgdt gdt_descr
  ret
```
废弃setup.s设置的GDT, 重定向GDTR, 因为后续一定会被内核用作其他用途. 其实是无法在setup.s直接一步到位设置GDT的  

```
call setup_gdt
movl $0x10,%eax    ; reload all the segment registers
mov %ax,%ds    ; after changing gdt. CS was already
mov %ax,%es    ; reloaded in 'setup_gdt'
mov %ax,%fs
mov %ax,%gs
lss _stack_start,%esp
```
重定向GDTR后把相关的数据段寄存器全部更新(段限长变了). A20是否工作通过回绕去验证, 不再赘述  

```
.align 2
setup_paging:
  movl $1024*5,%ecx    /* 5 pages - pg_dir+4 page tables */
  xorl %eax,%eax
  xorl %edi,%edi      /* pg_dir is at 0x000 */
  cld;rep;stosl
  movl $pg0+7,_pg_dir    /* set present bit/user r/w */
  movl $pg1+7,_pg_dir+4    /*  --------- " " --------- */
  movl $pg2+7,_pg_dir+8    /*  --------- " " --------- */
  movl $pg3+7,_pg_dir+12    /*  --------- " " --------- */
  movl $pg3+4092,%edi
  movl $0xfff007,%eax    /*  16Mb - 4096 + 7 (r/w user,p) */
  std
1:  stosl      /* fill pages backwards - more efficient :-) */
  subl $0x1000,%eax
  jge 1b
  xorl %eax,%eax    /* pg_dir is at 0x0000 */
  movl %eax,%cr3    /* cr3 - page directory start */
  movl %cr0,%eax
  orl $0x80000000,%eax
  movl %eax,%cr0    /* set paging (PG) bit */
  ret      /* this also flushes prefetch-queue */
```
把4个页表全部清空, 手动设置0x00000开头的页目录的前4项, 从第4项页表最后一项开始从0x0FFF007慢慢递减, 0xFFF正好是4096项  
设置页目录基地址寄存器CR3为0x0000, 设置CR0:PG = 1启动分页寻址机制. 现在, 在这样设置的分页机制下, 内核线性地址和物理地址其实是线性映射过去的, 或者说相同  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 页目录和页表结构可参照Intel手册 Intel 3A Chapter 4.3 32-bit paging
{: .prompt-info }
<!-- markdownlint-restore -->

```
  pushl $0    # These are the parameters to main :-)
  pushl $0
  pushl $0
  pushl $L6    # return address for main, if it decides to.
  pushl $_main

setup_paging:
  ;...
  ret      /* this also flushes prefetch-queue */
```
通过压栈之后的ret指令, 伪造中断返回现场, "返回"main函数  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 至此, boot阶段终于结束, 正式进入内核代码的世界了!
{: .prompt-info }
<!-- markdownlint-restore -->

## 从main到内核模块组成及分析

### 内核的一系列初始化

```c
mem_init(main_memory_start,memory_end);
  for (i=0 ; i<PAGING_PAGES ; i++)
    mem_map[i] = USED;
  i = MAP_NR(start_mem);
```
设置后15MB的页框全为0d100, 拉取出高速缓冲后的内存(主内存), 置对应mem_map为0d0  

```c
set_trap_gate(0,&divide_error);
  #define _set_gate(gate_addr,type,dpl,addr) \
  __asm__ ("movw %%dx,%%ax\n\t" \
    "movw %0,%%dx\n\t" \
    "movl %%eax,%1\n\t" \
    "movl %%edx,%2" \
    : \
    : "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
    "o" (*((char *) (gate_addr))), \
    "o" (*(4+(char *) (gate_addr))), \
    "d" ((char *) (addr)),"a" (0x00080000))
```
和head.s构建中断描述符类似,最终将%%eax(段选择符|中断服务程序地址低16bit)、%%edx(中断服务程序地址高16bitl标志)分别赋到对应中断描述符表,进行中断门/陷阱门/系统调用门的挂接  

```c
#define inb_p(port) ({ \
unsigned char _v; \
__asm__ volatile ("inb %%dx,%%al\n" \
  "\tjmp 1f\n" \
  "1:\tjmp 1f\n" \
  "1:":"=a" (_v):"d" (port)); \
_v; \
})
```
内联汇编: 指令:inb %%dx,%%al 输出:"=a" (_v): 输入:"d" (port)  
顺便做一个延时  

```c
void rs_init(void)
  set_intr_gate(0x24,rs1_interrupt);
  set_intr_gate(0x23,rs2_interrupt);
  init(tty_table[1].read_q.data);
  init(tty_table[2].read_q.data);
  outb(inb_p(0x21)&0xE7,0x21);
```
设置串口设备的中断门, 初始化对应结构体, 开启PIC的IRQ3&IRQ4(0xE7)  

```c
sched_init();
  set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
  set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));
```
重设在head.s留下的GDT, 初始化第0号进程对应项, 将后续所有TSS、LDT清零  

```c
ltr(0);
lldt(0);
outb_p(0x36,0x43);      /* binary, mode 3, LSB/MSB, ch 0 */
outb_p(LATCH & 0xff , 0x40);    /* LSB */
outb(LATCH >> 8 , 0x40);    /* MSB */
set_intr_gate(0x20,&timer_interrupt);
outb(inb_p(0x21)&~0x01,0x21);
set_system_gate(0x80,&system_call);
```
这里比较重要, 关乎进程0的关键数据, 包括:  
人工预先设置好任务0数据结构各字段的值(即init_task)、在全局描述符表中添入任务0的任务状态段(TSS)描述符和局部描述符表(LDT)的段描述符,并把它们分别加载到任务寄存器 tr 和局部描述符表寄存器 ldtr 中  

设置外部定时器外设发生10ms中断, 挂接int 0x20中断, 打开IRQ 0x21(时钟中断)  
挂接syscall的系统调用门 int 0x80   

```c
while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
  h->b_data = (char *) b;
  h->b_prev_free = h-1;
  h->b_next_free = h+1;
  h++;
}
```
左闭右开, 所以while里是>=号, 表示h和b正好对撞  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 所有初始化结束后, 开中断 sti();
{: .prompt-info }
<!-- markdownlint-restore -->

### 进程切换原理
```c
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
  "pushl $0x17\n\t" \
  "pushl %%eax\n\t" \
  "pushfl\n\t" \
  "pushl $0x0f\n\t" \
  "pushl $1f\n\t" \
  "iret\n" \
  "1:\tmovl $0x17,%%eax\n\t" \
  "movw %%ax,%%ds\n\t" \
  "movw %%ax,%%es\n\t" \
  "movw %%ax,%%fs\n\t" \
  "movw %%ax,%%gs" \
  :::"ax")
```
lss _stack_start,%esp 这个指令会加载段寄存器, 读取stack_start低4字节为esp, 高2字节为ss  
iret会弹出压栈的数据, 所以伪造现场(ss|esp|eflaga|cs|eip)  
0x17(0b000|10 第二项|1 LDT|11 用户特权级), 0xf(0b|01 第一项|1 LDT|11 用户特权级), 把标号1的偏移地址eip也压栈. 执行iret后, CPU自动弹出(ss|esp|eflaga|cs|eip), 变为用户态, 重新加载所有数据段寄存器, 用户栈(user_stack)  

代码段寄存器其实是内核代码段, 但运行在用户态  

由于特权级发上了变化, 段寄存器DS/ES/FS/GS的值变得无效, 此时CPU会把这些段寄存器清零. 因此在执行了iret指令后需要重新加载这些段寄存器  

```c
#define _syscall0(type,name) \
type name(void) \
{ \
long __res; \
__asm__ volatile ("int $0x80" \
    : "=a" (__res) \
    : "0" (__NR_##name)); \
if (__res >= 0) \
    return (type) __res; \
errno = -__res; \
return -1; \
}
```
0x80中断CPU会把ss|esp|eflaga|cs|eip依次压栈, 传参%eax = 2  

```
_system_call:
  cmpl $nr_system_calls-1,%eax
  ja bad_sys_call
  push %ds
  push %es
  push %fs
  pushl %edx
  pushl %ecx      # push %ebx,%ecx,%edx as parameters
  pushl %ebx      # to the system call
  movl $0x10,%edx     # set up ds,es to kernel space
  mov %dx,%ds
  mov %dx,%es
  movl $0x17,%edx     # fs points to local data space
  mov %dx,%fs
  call _sys_call_table(,%eax,4)
```
大多数代码段都是非一致代码段. 对于这些段,程序的控制权只能转移到具有相同特权级的代码段中,除非转移是通过一个调用门进行  
如果索引值指向中断门或陷阱门,则处理器使用与CALL指令操作调用门类似的方法调用异常或中断处理过程  
int 0x80系统调用门的DPL是3, 代表可从用户态访问这个GDT表项, 其实这也就是门的作用  
如果处理过程将在更高优先级运行(如0级内核态)上执行时就会发生堆栈切换操作. 堆栈切换过程如下:  
1. 处理器从当前执行任务的TSS段中得到中断或异常处理过程使用的用户态堆栈的段选择符和栈指针(例如 tss.ss0、tss.esp0)然后处理器会把被这个的栈选择符和栈指针压入新栈(内核栈)中  
2. 压入EFLAGS、CS和EIP  
3. 有错误码就压错误码进去  

可以想象如果不是在更高优先级运行, 上述步骤1. 是不做的, 执行栈切换操作的目的是为了防止高特权级程序由于栈空间不足而引起崩溃,同时也为了防止低特权级程序通过共享的堆栈有意或无意地干扰高特权级的程序, 这种栈分离是很常见的  
返回的时候就执行特权级检查, 如需则可切换回调用者的栈  

总结一下, int 0x80 -> CPU根据IDTR取出IDT的入口, 选择对应中断描述符表, 取出[段选择符:EIP], 切到内核态(复杂的特权级验证或CPL改变/栈切换/压栈, 涉及一致性和非一致性代码), 查出_system_call函数地址. 压栈段寄存器和传参调用参数, 段寄存器指向GDT内核数据, %fs指向LDT, 根据当前GDT查找基于_sys_call_table偏移对应位置的函数作为系统调用的入口函数  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 现代Linux由于x86的历史包袱, 绕开了段式管理, 实质上是页式管理在起作用  
{: .prompt-info }
<!-- markdownlint-restore -->

```c
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
        long ebx,long ecx,long edx,
        long fs,long es,long ds,
        long eip,long cs,long eflags,long esp,long ss)
    p->tss.esp0 = PAGE_SIZE + (long) p;
    p->tss.ss0 = 0x10;
```
none是call _copy_process自动压入的返回地址  
eip, cs, eflags, esp, ss是int 0x80从用户态进内核时, CPU自动压的用户态现场  
将内核栈地址设为该页顶部, 并且将内核栈段寄存器设为0x10(内核数据段长度16MB, 即全部覆盖). 这里一堆参数, 都是_system_call->_sys_fork调用链的栈布局的影响, 具体每个部分作用估计要读后续代码具体理解  

经过分段机制变换,内核代码和数据段位于线性地址空间的头 16MB 范围内,再经过分页机制变换,它被直接一一对应地映射到 16MB 的物理内存上. 因此对于内核段来讲其线性地址就是物理地址  

每个表项由页框地址、访问标志位、脏（已改写）标志位和存在标志位等构成  

```c
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t"
    "jne 1f\n\t"
    "movb $1,1(%%edi)\n\t"
    "sall $12,%%ecx\n\t"
    ...
    "movl %%edx,%%eax\n"
    "1:"
    :"=a" (__res)
    :"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
    "D" (mem_map+PAGING_PAGES-1)
    :"di","cx","dx");
return __res;
}
```
scasb从mem_map最后向前遍历, 当找到时%%edi其实已经指向的下一个要比较的地址, 所以后续要再+1才是真正空闲的物理地址  
初始化的时候其实是不会动最前面1MB位置的, 这部分已经留给内核线性映射了, 而且分配的时候默认加LOW_MEM也体现了这一点  


```c
code_limit=get_limit(0x0f);
  #define get_limit(segment) ({ \
  unsigned long __limit; \
  __asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
  __limit;})
```
LSL eax, 0x0f 读取索引index=1 -> LDT[1] 取 descriptor.limit(INIT_TASK里的ldt成员) eax = limit  

```c
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
  unsigned long * from_page_table;
  unsigned long * to_page_table;
  unsigned long this_page;
  unsigned long * from_dir, * to_dir;
  unsigned long nr;

  if ((from&0x3fffff) || (to&0x3fffff))
    panic("copy_page_tables called with wrong alignment");
  from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
  to_dir = (unsigned long *) ((to>>20) & 0xffc);
  size = ((unsigned) (size+0x3fffff)) >> 22;
  for( ; size-->0 ; from_dir++,to_dir++) {
    if (1 & *to_dir)
      panic("copy_page_tables: already exist");
    if (!(1 & *from_dir))
      continue;
    from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
    if (!(to_page_table = (unsigned long *) get_free_page()))
      return -1;  /* Out of memory, see freeing */
    *to_dir = ((unsigned long) to_page_table) | 7;
    nr = (from==0)?0xA0:1024;
    for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
      this_page = *from_page_table;
      if (!(1 & this_page))
        continue;
      this_page &= ~2;
      *to_page_table = this_page;
      if (this_page > LOW_MEM) {
        *from_page_table = this_page;
        this_page -= LOW_MEM;
        this_page >>= 12;
        mem_map[this_page]++;
      }
    }
  }
  invalidate();
  return 0;
}
```
由于是内核空间, 所以from_dir总是_pg_dir = 0, 线性地址恒等于物理地址, `from_page_table = (unsigned long *) (0xfffff000 & *from_dir);`所以才可以这么写.   
if (!(1 & this_page)) 如果没使用就跳过  

if (this_page > LOW_MEM) 只有当调用 fork() 的父进程代码处于主内存区(页面位置大于 1MB)时才会执行. 这种情况需要在进程调用 execve(), 并装载执行了新程序代码时才会出现   
进程1复制进程0的页表实现不同逻辑地址通过不同页表映射到相同物理地址空间  

最后fork()结束了, 由于fork()返回1 所以init()不会被执行, 会继续执行触发进程调度  

```c
int copy_process()
  p->tss.eip = eip;
  p->tss.eax = 0;
```
这两行是新fork出的进程的下一条指令和fork()返回值, 可以看出子进程和父进程执行的指令是一致的, 但是返回值是0  

```c
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \
  "je 1f\n\t" \
  "movw %%dx,%1\n\t" \
  "xchgl %%ecx,_current\n\t" \
  "ljmp %0\n\t" \
  "cmpl %%ecx,_last_task_used_math\n\t" \
  "jne 1f\n\t" \
  "clts\n" \
  "1:" \
  ::"m" (*&__tmp.a),"m" (*&__tmp.b), \
  "d" (_TSS(n)),"c" ((long) task[n])); \
}
```
通过ljmp TSS选择符_TSS(n)定位对应的描述符, 这个应该是线性地址, 然后由于内核当前做过了初始的恒等映射, 所以在这种恒等映射的分页机制下线性地址就是物理地址, 直接在对应TSS指针里保存任务现场  


GDT/TSS/LDT大体形式
```
 63                56 55 54 53 52 51        48 47 46   45 44 43      40  
+-------------------+--+--+--+--+------------+--+-------+--+----------+  
|    Base 31..24    |G |D |L |A | Limit19..16|P |  DPL  |S |  TYPE    |  
+-------------------+--+--+--+--+------------+--+-------+--+----------+  
 39                32 31                              16 15        0  
+-------------------+----------------------------------+------------+  
|    Base 23..16    |            Base 15..0            | Limit15..0 |
+-------------------+----------------------------------+------------+  
```
### 缓冲区分配算法
文件系统这边《UNIX操作系统设计》讲的比较通俗易懂  

一个磁盘块只能对应一个高速缓冲区  
散列队列用于快速查找, 空闲表用于实现LRU算法  
一个缓冲区总是在某个散列队列上, 但它可以在也可以不在空闲表上  

将getblk缓冲区分配算法各个情况由简单到复杂列一下:  
1. hash有, 且缓冲区空闲
2. hash没有, 且空闲表至少有一个缓冲区
3. hash没有, 且空闲表中只有脏的缓冲区
4. hash没有, 且空闲表没有正在IO或脏的缓冲区
5. hash有, 且缓冲区非空闲

```
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
struct buffer_head * getblk(int dev,int block){
repeat: 
  if(散列队列找到了){
    if(缓冲区忙){
      sleep(等待缓冲区空闲事件);
      goto repeat; // 对应散列队列里的块正好被情况4替换出去了, 需要重查状态
    }
    从空闲表摘下一个缓冲区;
    标记为忙;
    return(缓冲区);
  }
  else{
    if(空闲表空){
      sleep(等待空闲表有余量事件);
      goto repeat; // 有可能很多个进程都在睡, 只有一个能成功抢到, 没抢到的进程只能重来
    }
    从空闲表摘下一个缓冲区;
    if(缓冲区是延迟写){
      启动磁盘IO;
      goto repeat;
    }
    如果旧缓冲区在散列队列就摘下;
    把新缓冲区放到对应散列队列里;
    return(缓冲区);
  }
}
```

```c
if (bh = get_hash_table(dev,block))
	return bh;
```
对第一种情况来说上述代码可能暗藏着, 如果buffer没lock就肯定在空闲表里, 从空闲表摘除用这个代表的bh->b_count++;  

```c
wait_on_buffer(bh);
if (bh->b_count)
  goto repeat;
```
空闲buffer还在IO, 需要睡眠, 如果醒来后变成忙了就repeat   

```c
if (find_buffer(dev,block))
  goto repeat;
```
这段是可能本来不在散列里的块, 由于我们的睡眠, 被其他进程缓存进散列里了  

```c
void brelse(struct buffer_head * buf)
{
  if (!buf)
    return;
  wait_on_buffer(buf);
  if (!(buf->b_count--))
    panic("Trying to free free buffer");
  wake_up(&buffer_wait);
}
```
这段代码设计和UNIX书中差别很大, 是靠引用计数判断是否空闲的, 有可能睡眠的进程白白被叫起来, 起来了发现条件没满足, 还要重睡  

**《UINX操作系统设计》第三章习题**  
逻辑基点: 同一个磁盘块在缓冲区池里必须只有一个缓冲  

1. **在算法getblk中, 如果内核要从空闲表中摘下一个缓冲区, 则它必须提高处理机优先级, 以便在检查空闲表之前封锁中断. 这是为什么?**  
因为磁盘IO中断完成后也会在中断里操作空闲表, 中断可能会打断内核正在操作空闲表的过程   
2. **在算法 brelse 中, 如果缓冲区内容无效, 则内核把该缓冲区加入空闲表队列头部. 如果缓冲区内容无效,该缓冲区应该出现在散列队列中吗?**  
如果该缓冲区仍然对应某个设备块号它就会出现在散列队列中, 和缓冲区内容是否有效无关, 内容的有无效只会影响缓冲区替换算法LRU原则(也就是插入在空闲表头部还是尾部)  
3. **假设内核进行一个块的延迟写, 当另一个进程从它的散列队列中取那个块时会发生什么? 从空闲表中呢?**  
如果getblk从散列队列中找到就说明进程就是要找这个块, 而且里边内容也是最新的, 此时直接把缓冲区从空闲表摘下标记为忙, 返回给该进程. 这也是延迟写的意义所在: 省去了一次磁盘I/O  
如果getblk在空闲表中取到这个块, 那就说明进程请求的不是原来的块. 它单纯只想弄个缓冲区, 那就只能启动磁盘I/O, 完成后另寻缓冲区  
4. **描述一种在算法bread中的缓冲区数据已经有效的情况**  
如果进程请求的块已经被其他进程读入缓冲区, 在散列队列缓存, 并且这个缓冲区已经被释放, bread再次请求这个块就是缓存命中  
5. **描述一个从缓冲区池请求及接收任何一个空闲缓冲区的算法. 将这一算法与getblk比较**   
检查空闲表, 是空就睡眠; 去除缓冲区, 被标记延迟写就启动IO等待, 然后重新查找; 否则就置忙然后返回给进程  
这个算法不会维护块号在缓冲区的唯一性  
6. ***在算法getblk 中, 在检查一个块是否正处于忙状态之前,内核必须提高处理机优先级以封锁中断(在正文中这一点没有表示出来). 这是为什么?**  
如果在检查出缓冲区不忙, 尚未把它置位前, 磁盘IO中断或其他内核路径可能会改变这个缓冲区的状态. 也就是这检查和置位必须是原子的, 同样有这类问题的地方还有如果缓冲区忙则睡眠. linux-0.11中wait_on_buffer是有这一手操作的  
7. ***如果几个进程竞争一个缓冲区,内核担保没有一个进程永远睡眠,但是它不保证进程不会总也得不到使用缓冲区的机会. 重新设计getblk 以保证一个进程最终能用上一个缓冲区**  
linux-0.11的getblk理论上是会有进程一直得不到运行的可能性的. 问题就是brelse是全部唤醒后重新竞争, 那从这里入手维护一个等待队列就好了, 睡眠就入队, brelse时出队, 实现公平竞争  
// todo 具体代码有空再写  

### 底层文件系统算法
```
struct m_inode * iget(int dev,int nr){
repeat1:
  if(从inode缓冲区中没有lock/dirty的inode){
    panic;
  }
  if(inode缓冲区lock){
    sleep(缓冲区空闲事件);
  }
  if(inode脏){
    write_inode算法;
    sleep(inode空闲事件);
  }
  if(检查对应inode缓冲引用非0){
    goto repeat1;
  }
  找到一个空闲inode缓冲节点;

repeat2:
  if(inode缓冲区有对应索引节点缓冲区){
    if(索引节点lock){
      sleep(索引节点缓冲区解锁事件);
    }
    // 为什么这里多一步检查inode脏, 可能因为脏也无所谓, 没必要写进磁盘, 省了一次IO
    if(索引节点状态变化){
      goto repeat2;
    }
    索引节点引用加一;
    // todo 处理挂载点
    释放空闲inode缓冲节点;
    return inode;
  }

  // 以下三行是read_inode算法, 和write_inode雷同
  索引节点缓冲区加锁;
  bread对应索引节点;
  索引节点缓冲区解锁;
  return inode;
}

// 将inode写入磁盘
static void write_inode(struct m_inode * inode){
  inode上锁; (可能睡眠)
  再次检查inode是否脏;
  读超级块拿到对应inode块;
  读inode块; (算法bread)
  改写缓冲区对应inode的数据;
  设置inode不脏;
  设置缓冲区脏;
  brelse缓冲区;
  解锁inode;
```
如果活跃的inode超限, 直接报错, 为什么不睡眠等待可用呢? 因为这是用户控制的而非内核. 如果有只能找到inode是上锁的或脏的, 内核选择休眠, 因为上锁和脏数据何时恢复是由内核可以保证的. 这是个需要注意的点  

empty = get_empty_inode();里涉及睡眠操作, 如果等到cache miss再去分配empty就要重新再查一次inode_table, 控制流变得复杂了. 所以往往先把所需资源提前准备好, 再进入检查阶段  

```
void iput(struct m_inode * inode){
  确认inode稳定; (wait_on->lock)
  // todo 处理管道
  if(inode是临时的){ // 例如inode过渡态(get_empty_inode)下就要iput
    inode引用减1;
  }

repeat:
  if(inode引用大于1){
    inode引用减1;
    return; // 直接返回是因为只有inode要释放的时候才需要做下述清理操作
  }

  if(文件link为0){
    释放所有相关数据块;
    free_inode算法;
    return;
  }

  if(inode缓冲区脏){
    write_inode算法;
    sleep(inode缓冲区空闲事件);
  }
  inode引用减一;
}
```
iput上来就先检查inode是否在IO, 如果在IO还去修改的话可能write_inode这类数据是半更新的  

if (inode->i_dirt)条件要repeat的原因还是睡眠(需要bread)之后系统或者说inode状态可能会变(i_count ,i_nlinks)  

考虑这样一种情况, 文件在open之后, 立即进行ulink操作, 这样其实文件开始可以正常通过打开的文件描述符fd进行读写的, 我们来分析一下:  
ulink本质上是对inode做了一次nlink--操作, 随后iput来释放对应inode, 但是iput其实只会对inode的引用计数减一, 即使nlink变为0也不会清理inode. 在open的时候会做iget来获取inode持有引用引用加1, 在做ulink的时候内核不会凭空知道是哪个inode, 它其实也要iget一下, 那么这样这份inode的引用又加1了, 至少是2. 随后ulink在最后会iput一下, 这下iput的只是自己那份"临时引用". 这样就形成了nlink已经变为0, 但引用非零的情况, 这时inode对应的资源还没被释放, open是可以正常读写文件的, 最后如果文件close了, 那么inode也就自动释放, 清理资源了. 这是个非常好的理解文件系统系统调用的例子, 下面是GPT生成的测试代码, Bochs模拟可以发现是预期现象    

UNIX书中p65有对ifree进行描述, 说如果超级块被锁直接返回, 为了避免竞争条件, 超级块里的只是小块缓存, 真正的真相在磁盘中也能找到  

```c
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(void)
{
    int fd, fd2, n;
    char buf[32];

    fd = open("tmpfile", O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        printf("open tmpfile failed\n");
        return 1;
    }

    write(fd, "hello", 5);
    lseek(fd, 0, 0);

    if (unlink("tmpfile") < 0) {
        printf("unlink failed\n");
        close(fd);
        return 1;
    }

    printf("unlink ok\n");

    fd2 = open("tmpfile", O_RDONLY, 0);
    if (fd2 < 0)
        printf("re-open after unlink: failed as expected\n");
    else {
        printf("re-open after unlink: unexpected success\n");
        close(fd2);
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("read from old fd failed\n");
        close(fd);
        return 1;
    }

    buf[n] = 0;
    printf("read from old fd ok: [%s]\n", buf);

    close(fd);
    printf("close old fd done\n");
    return 0;
}
```

![alt text](/assets/img/linux-summary/image.png)

```c
static int _bmap(struct m_inode * inode,int block,int create)
```
注意inode脏和bh脏的区别就好了  

```
struct m_inode * new_inode(int dev){
  分配一个空闲inode缓冲节点(); // @iget()->repeat1
  从位图找第一个为0的空位;
  设置inode脏;
  设置磁盘缓冲区脏;
  return inode;
}
```
```
void free_inode(struct m_inode * inode){
  直接获得inode位图后清零;
  缓冲区bh标脏;
}
```
Linux 0.11里不会以 Bach 书里那种 **"扫磁盘inode区, 睡眠后被别人抢同一空闲inode(4-16 分配索引节点的竞争条件)"** 的方式出问题  
因为它分配磁盘inode号靠的是内存中的 inode bitmap, 而new_inode()的“找 0 bit 并置 1”这段关键路径运行在0.11的单CPU、内核态不可抢占模型下, 普通进程B不能在中间插进来分到同一个inode. 在这里UNIX和LINUX的设计哲学不太一样(ifree的一个race条件[UNIX p66])  

最后的bh->b_dirt = 1不是说把s_imap[8]本身写回磁盘, 而是说s_imap中存储的inode位图块写回磁盘, 同理还有new_block逻辑位图块的操作  

```
int new_block(int dev){
  在block位图中搜索第一个空位;
  置位对应bit;
  设置位图缓冲区脏;
  bh = getblk();
  清空数据;
  设置bh缓冲区为脏且为最新;
  brelse(bh);
  return 位号;
}
```
```
void free_block(int dev, int block){
  if(在散列队列里找到块){
    brelse(块);
  }
  // 为什么这里不再由于睡眠进行些检查呢? 因为new_block本身也依赖位图, 位图实际清零之前睡眠不会影响这块缓冲区
  清空磁盘块位图对应bit;
  设置对应磁盘位图块为脏;
}
```
free_block先处理缓冲再处理位图  


**《UINX操作系统设计》第四章习题**  
1. **若一个进程,因发现高速缓冲区索引节点为上锁状态,而在算法iget中睡眠,为什么它醒来之后必须重新开始循环?**  
睡眠结束后无法保证inode的dev, nr和睡眠前一致, inode_table的目标值可能被其他进程改掉了  
2. **算法iget与iput没有要求提高处理机执行级以封锁中断. 这味着什么?**  
说明iget与iput不像getblk有磁盘IO中断会修改buffer的状态/空闲链表(UNIX中是这样的, Linux不会), 主要race出现在进程之间的竞争  
3. ***正如本章所描述的那样,超级块是一个磁盘块,并且除了包含空闲表之外还包含其他信息. 因此,超级块空闲表不能包含像在空闲磁盘块的链接表上的一个磁盘块中可能储存的那么多的空闲块号. 链接表上一个磁盘块中应存储多少个空闲块号才是最适宜的?**  
链接表上一个磁盘块中最适宜存放的空闲块号数目, 应当与超级块空闲表能容纳的块号数目相同, 这样补充空闲表是完全一致的 
4. ***从理论上说, 文件系统永远不会含有其索引节点号比被ialloc使用的“铭记”索引节点号小的空闲索引节点. 为什么这一断言有可能不成立呢?**  
inode的过渡态? 超级块被锁住ifree没把该inode号放进缓存表? 它虽然已经空闲,但没有进入超级块缓存?  
// todo 这个问题没想明白  
5. ***讨论用位图而不是块的链接表来记录空闲磁盘块的系统实现. 这一方案的优点和缺点各是什么?**  
位图的优点是“全局清楚、便于优化”, 缺点是“要专门存储并且分配时常要搜索”; 链接表则相反,优点是“取下一个块很直接”, 缺点是“缺少全局视图”  

```
// 根据dir的inode查找所有目录项, 返回和name一致的目录的inode
static struct buffer_head * find_entry(struct m_inode ** dir,
  const char * name, int namelen, struct dir_entry ** res_dir){
  获取dir的目录数据长度;
  特殊处理..和跨越挂载点的情况;
  读取磁盘目录数据; (算法bread)
  while(dir没搜索完){
    if(一块缓冲区已被搜索完){
      读取新的一块;
    }
    if(匹配name){
      return 目标的dir_entry;
    }
  }
}

// 搜索父目录inode, 并返回目标文件名和长度
static struct m_inode * dir_namei(const char * pathname,
  int * namelen, const char ** name){
  当前进程根节点inode引用加1;
  while(1){
    用‘/’分割pathname;
    在当前inode下搜索上述分割到的目录项名称; (算法find_entry)
    if(没找到){
      return NULL;
    }
    根据对应的dir_entry拿出inode编号;
    iput(当前inode); // 这段比较关键, 下边(sys_link)有对应的竞态条件分析
    下一次迭代的inode = iget();
  }
  单独解析目标文件名和长度;
  return 父目录的inode;
}

// 寻找对应目标路径的inode节点
struct m_inode * namei(const char * pathname){
  dir = dir_namei(); // 寻找pathname上层父目录的inode
  在目录索引表中寻找对应inode编号; (算法find_entry)
  获得目标文件inode; (算法iget)
  return inode;
}
```
namei以很多可复用的子函数构成. get_dir根据初始inode节点读取所有目录项, 从中线性查找和目标名称一样的目录名, 根据目录名对应出inode节点(dir_entry), iget一下, 然后基于这个inode继续线性查找, 直到最后父目录为止. 最后再调用find_entry找到最后的inode号, 然后在iget一下就结束了  

### 文件系统系统调用
```
int sys_open(const char * filename,int flag,int mode){
  找到一个空闲的文件表项;
  将这个文件表项赋值给用户文件描述表;
  引用加1; // 主要为了占位, 后续namei可能会导致睡眠, 防止又有进程取到了当前文件表项
  open_name算法; // 其中new_inode后标inode脏, 而后执行add_entry算法
  设置好文件表项;
}

static struct buffer_head * add_entry(struct m_inode * dir,
  const char * name, int namelen, struct dir_entry ** res_dir){
  读取dir对应的目录块缓冲;
  搜索可用的空闲inode号;
  if(没找到){
    加目录块数据长度;
    inode标脏;
  }
  if(inode空闲){
    目录块中写入用户的basename;
    缓冲区标脏;
  }
}
```
de->inode是0代表空目录项, add_entry之后是不能立马睡眠的, 源码里也有特别的NOTE注释说明这点  
文件表项的引用会受fork, dup等的影响  

```
int sys_read(unsigned int fd,char * buf,int count){
  根据fd取出inode;
  检查fd对应的文件表项的pos;
  if(文件)
    读文件; (算法file_read)
  if(管道)
    读管道; // 这里用的偏移量是文件表的, UNIX书中有名管道用的是索引节点的偏移量, 目的是多个进程共享偏移量
}

int file_read(struct m_inode * inode, struct file * filp, char * buf, int count){
  读取inode对应的数据块; (算法bmap, bread)
  处理pos不在块边界的情况;
  if(缓冲区存在){
    读数据;
  }
  else{
    读0; // 代表没分配磁盘块 leek?
  }
}
```

```c
int sys_write(unsigned int fd,char * buf,int count)
```
和sys_read对称  

sys_lseek就改变文件表的字节偏移量就可以了  

```
int sys_close(unsigned int fd){
  文件表fd对应引用减1;
  if(引用为0){
    释放一次fd对应的inode; (算法iput)
  }
}
```
有可能由于fork和dup导致sys_close仅仅让引用减1  

```
int sys_pipe(unsigned long * fildes){
  在文件表中找2个空的;
  在用户文件描述符表中再找2个空的赋值;
  获取索引节点; // 这里只获取4KB内核页面
  用户文件描述符传递给用户空间;
}

int write_pipe(struct m_inode * inode, char * buf, int count){
  while(没写完){
    if(管道满){
      唤醒读进程;
      sleep(管道可操作事件);
    }
    写数据;
    从用户buf取到内核管道;
  }
  唤醒读进程;
}
```

```
static int dupfd(unsigned int fd, unsigned int arg){
  找到用户文件描述符表中的第一个空槽;
  将fd对应的用户文件描述符赋给空槽;
  用户文件描述符引用加1;
}
```
如果一个文件被open了两次, 那么通过不同的fd读取的偏移量是各自分开的; 如果一个文件被dup复制了fd, 那么通过不同的fd读取的偏移量则是共享的  

```c
int sys_mount(char * dev_name, char * dir_name, int rw_flag){
  ...
  sb->s_imount=dir_i;
  dir_i->i_mount=1;
  dir_i->i_dirt=1;    /* NOTE! we don't iput(dir_i) */
}
```
应该是有竞态的考虑的, UNIX书中说[p99]在读取超级块时可能会睡眠, 这时其他进程也去挂载相同文件系统可能会发生莫名其妙的的事情, 所以搞了个使用标志, 但是我看Linux 0.11是没有的  
inode应该一直不被iput, 否则sb->s_imount就无效了  

```c
int sys_umount(char * dev_name){
  ...
  put_super(dev);
  sync_dev(dev);
}
```
结束挂载时记得要把所有内存中有关超级块的信息同步到磁盘, 把所有属于这个块设备的数据同步到磁盘  

```c
int sys_link(const char * oldname, const char * newname){
  ...
  de->inode = oldinode->i_num;
  bh->b_dirt = 1;
  brelse(bh);
  iput(dir);
  oldinode->i_nlinks++;
  oldinode->i_ctime = CURRENT_TIME;
  oldinode->i_dirt = 1;
  iput(oldinode);
}
```
把这个新的目录项inode变成要link的inode, 然后写入对应目录块, oldinode元数据变了所以标脏  

```c
int sys_unlink(const char * oldname, const char * newname){
  ...
  de->inode = 0;
  bh->b_dirt = 1;
  brelse(bh);
  inode->i_nlinks--;
  inode->i_dirt = 1;
  inode->i_ctime = CURRENT_TIME;
  iput(inode);
  iput(dir);
}
```
sys_unlink把目录项给置零, 对应目录块标脏, 然后nlink减1, 上边分析过nlink和iput一些微妙关系  

还有一些注意的点:
1. UNIX中namei会上锁遍历到的inode, 导致link是会死锁的, Linux 0.11不会这样, namei每次iget之后都会iput  
2. 文件系统一致性
当系统发生故障时, 为了使文件系统的破坏达到最小程度, 内核要按某种次序来进行写磁盘操作. 例如, 当内核从一个父目录中清除一个文件名时, 它要在清除该文件的内容及释放其索引节点之前,同步地将父目录写到磁盘上去  

如果系统先将文件目录表项清零, 随后发生故障, 那么只会留下一个不会被释放的"孤儿inode", 系统仍然会正常工作, 只是有一部分空间无法回收    

如果系统现将索引节点的nlink减1, 随后发生故障, 那么有可能某个链接文件指向一个莫名其妙的位置, 有可能是新分配的文件, 文件结构被破坏了  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 整个文件系统看下来, 有非常多的TOCTTOU(Time Of Check To Time Of Use)竞态考虑
{: .prompt-info }
<!-- markdownlint-restore -->

### 进程控制
以下理论概念整合自《UNIX操作系统设计》  

进程的上下文由用户级上下文(进程正文、数据、栈、共享数据), 寄存器上下文, 系统级上下文组成. 系统级上下文包括静态部分:进程表项, u区, 区表; 动态部分:寄存器上下文, 核心栈  
在发生中断时, 或一个进程发生系统调用时, 或进程进行上下文切换时, 内核就压入一个上下文层. 当内核从处理中断中返回, 或一个进程在完成其系统调用后返回用户态, 或一个进程进行上下文切换时, 内核就弹出一个上下文层  

```
// 处理中断
void intand(){
  保存(压入)当前上下文层;
  查找中断向量;
  执行中断服务程序;
  恢复(弹出)前一上下文层;
}
```

```
_system_call:
  ...
  call _sys_call_table(,%eax,4) //_sys_fork
  pushl %eax
  ...
ret_from_sys_call:
  movl _current,%eax    # task[0] cannot have signals
  cmpl _task,%eax
  je 3f
  cmpw $0x0f,CS(%esp)    # was old code segment supervisor ?
  jne 3f
  cmpw $0x17,OLDSS(%esp)    # was stack segment = 0x17 ?
  jne 3f
  ...
  call _do_signal
  popl %eax
3:  popl %eax
  popl %ebx
  popl %ecx
  popl %edx
  pop %fs
  pop %es
  pop %ds
  iret
```

```
_sys_fork:
  call _find_empty_process
  testl %eax,%eax
  js 1f
  push %gs
  pushl %esi
  pushl %edi
  pushl %ebp
  pushl %eax
  call _copy_process
  addl $20,%esp
1:  ret
```

重新分析一下[进程切换原理](#进程切换原理)没分析完全的地方:  
switch_to宏里的对TSS描述符进行ljmp指令, 于是上下文通过tss指针来存储, 并取出TSS状态里的LDTR寄存器、CR3、EFLAGS寄存器、EIP 寄存器以及通用寄存器和段选择符[Linux内核完全注释V5.0 P132]  
这时如果用户进程发生了_system_call, 通过加载0x17(任务数据段)段选择符进%fs, 以后和和用户进程地址空间的交互就可以通过%fs来进行  
call _copy_process会先把返回地址给压栈, 这部分是x86调用约定, addl $20,%esp负责把上边作为参数压栈的5个参数一并弹出  
然后我们再来分析一下_system_call的栈布局变化, 刚进入系统调用 ->  
`0:=eip | 4:=cs | 8:=eflags | C:oldesp | 10:oldss`  
进入_copy_process ->  下面可以对应上copy_process的参数布局了, 这里有关过程调用的栈帧可能有点问题, 不过无伤大雅  
`0:ret_to__sys_fork | 4:eax | 8:ebp | C:edi | 10:esi | 14:gs | 18:ret_to__system_call | 1C:ebx | 20:ecx | 24:edx | 28:fs | 2C:es | 30:ds | 34:eip | 38:cs | 3C:eflags | 40:oldesp | 44:oldss`  
执行到ret_from_sys_call ->  
`0:eax | 4:ebx | 8:ecx | C:edx | 10:fs | 14:es | 18:ds | 1C:eip | 20:cs | 24:eflags | 28:oldesp | 2C:oldss`  


```c
static inline void __sleep_on(struct task_struct **p, int state)
{
    struct task_struct *tmp;

    if (!p)
        return;
    if (current == &(init_task.task))
        panic("task[0] trying to sleep");
    tmp = *p;  // 把任务等待队列赋给tmp, 第一次睡眠tmp是NULL
    *p = current; // 把当前任务(待睡眠)插入任务等待队列头部
    current->state = state;
repeat: schedule();
    if (*p && *p != current) {  // 如果不成立, 说明在睡眠期间没有新的任务插入等待队列
        (**p).state = 0;
        current->state = TASK_UNINTERRUPTIBLE;
        goto repeat;
    }
    if (!*p)
        printk("Warning: *P = NULL\n\r");
    if (*p = tmp)
        tmp->state=0;
}
```
sleep的时候内核栈不是空的, 当一个任务进入内核态执行时, 其内核态堆栈总是空的  
如果在睡眠期间没有新的任务插入等待队列就直接执行不进入`if (*p && *p != current)`, 直接执行`*p = tmp`也就是任务等待头为NULL  
如果在睡眠期间有新的任务插入等待队列就执行`if (*p && *p != current)`把当前任务置为非中断等待状态, 并且唤醒新插入的任务. 轮到新任务执行(期间假设无新任务插入) `if (*p = tmp)`在这里由新任务唤醒之前被插队的旧任务. 等到旧任务真正执行的时候置任务等待头为NULL 
这种设计可以只wakeup一次唤醒多个在同一事件等待的进程, 不需要重复wakeup, 不过这写法有些难以理解...

// todo interruptible_sleep_on的repeat逻辑分析(可能是因为信号?)  

系统启动后终端shell的行为:  
```sh
#!/bin/sh 
echo "arg0: $0"
echo "arg0: $1"
echo "arg0: $2"
```
在源码中添加printk执行./t.sh查看, 发现如下现象  
![alt text](/assets/img/linux-summary/image-1.png)  
shell第一次do_execve因为权限问题直接退出了, 而后看起来像触发了兜底机制, shell去解析发现#!, 构造参数进行第二次do_execve(/bin/sh, argv...), /bin/sh是个二进制文件, 可以发现inode = 9也正是/bin/sh所在的inode号  

```sh
#!/bin/echo Hello
echo "this should not run directly"
```

/bin/echo内容如下  
```sh 
#!/bin/bash
echo $*
```
修改test.sh为上述脚本, 执行./test.sh发现#!相关处理, 输出如下  
![alt text](/assets/img/linux-summary/image-2.png)  
shell第一次do_execve也退出了, shell去解析发现#!, 构造参数进行第二次do_execve(/bin/echo, argv...), 然后发现/bin/echo又是个脚本, 触发了do_execve函数内部的#!机制, 重新去restart_interp, 摇身一变成了/bin/sh, 使用重新构造的参数表打印$*  

可以预见如果执行sh ./test.sh, 强制会按/bin/sh运行./test.sh, 那么就会输出this should not run directly, 事实也确实如此  
![alt text](/assets/img/linux-summary/image-3.png)  

```
_sys_execve:
  lea EIP(%esp),%eax
  pushl %eax
  call _do_execve
  addl $4,%esp
  ret

int do_execve(unsigned long * eip,long tmp,char * filename,
  char ** argv, char ** envp){
  查找filename对应的inode;
  读取第一块(算法bread);
  数argv和envp指针数组的个数;
repeat:
  鉴权;
  if(filename由#!开头){
    重构用户栈参数表;
    设置fs为内核数据段; // 因为namei用的是fs
    获取#!后的解释器inode;(算法namei)
    goto repeat;
  }
  释放当前进程的代码段数据段;
  构造参数表;(32个页表)
  设置%fs为0x17;
  映射32个页表;
}
```
tmp还是ret_to__system_call call指令自动压栈的. 为什么只改EIP和ESP, 因为要修改的是入口地址用户栈顶位置  

copy_strings就是构造参数表的重要函数, %ds在sys_call的时候设置为kernel space %fs设置为user space. 因为from_kmem为1的时候代表argv*在内核空间(filename在内核栈), argv**在用户空间, 是由shell程序构造的. 因为from_kmem为2的时候都在内核空间. 因为都是通过%fs获取数据所以需要修改%fs. 我们删掉为1或2的部分, 直接panic了, 如下图  
![alt text](/assets/img/linux-summary/image-4.png)  
但是删掉get_free_page()前后from_kmem为2的判断是没事的, 估计是防御性编程(调用内核函数时恢复临时改变的%fs)  

p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
p应该是从64MB处减去参数的个数的位置, 就是把内核buf里的参数位置重映射到用户栈空间  

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 这部分shell程序行为不是很好分析啊, 可能还是不对
{: .prompt-info }
<!-- markdownlint-restore -->

### 分页变换机制
页表结构如下：  
```
31                                     12 11 10 9 8   7  6  5  4  3 2 1 0  
+--------------------------------------+--+--+--+--+--+--+--+--+--+-+-+-+  
|       Page Table Base Address        |AVL |   |  |A |PCD|PWT|U/S|R/W|P|  
+--------------------------------------+--+--+--+--+--+--+--+--+--+-+-+-+  
```
  
![alt text](/assets/img/linux-summary/image-5.png)  

```
@page     物理地址  
@address   线性地址
unsigned long put_page(unsigned long page,unsigned long address)
{
  逻辑地址根据计算页目录项;(一项4字节)
  if(页目录不存在){
    分配一个页表;
    将页表对应地址给页目录填好
  }
  根据页目录项解析出页表的首地址;
  页表首地址加偏移10bit的项就是page;
  return page;
}
```
`page_table[(address>>12) & 0x3ff] = page | 7;`这里用的是`(address>>12) & 0x3ff`, 有的地方是`(address>>10) & 0xffc`, 对4字节取整了, 这部分注意下  

缺页异常:  
```
@address线性地址
void do_no_page(unsigned long error_code,unsigned long address){
    unsigned long *page_dir_addr = (address >> 20) & 0xffc; // 页目录地址
    unsigned long page_dir = *page_dir_addr;
    if(page_dir & 1){
        调页逻辑; // 就是后面写的swap_in
    }
    unsigned long liner_addr = address & 0xfffff000;
    int block = (liner_addr - start_code) / 1024 + 1;
    地址在库区域操作, share后返回;
    地址在数据/代码区, share后返回;
    地址是栈区直接申请并映射address(高20bit是地址);
    上述操作都不行, 只能在磁盘read一个page了;
}
```
如果一页code超出了current->end_data的范围, 就把超出的部分清零, 如果是库文件部分就不清零  

```
@address 逻辑地址
static int try_to_share(unsigned long address, struct task_struct * p){
  把逻辑地址都转换成线性地址;
  if(源二级页表对应物理地址不存在或脏){
    reutrn;
  }
  page = 目的进程的二级页表不存在就分配一个;
  目的进程对应页目录 = page;
  目的进程二级页表对应项的值 = 源进程二级页表对应项的值; // 共享
  目的、源的对应二级页表全部开启写保护;
  对应物理地址mem_map引用加1;
}

@address 逻辑地址
static int share_page(struct m_inode * inode, unsigned long address)
{
  p = 搜索和当前执行进程的inode节点一致的进程;
  return 0;
}
```
do_no_page没法共享的就不共享了, 比如栈  

```c
void un_wp_page(unsigned long * table_entry)
{
  unsigned long old_page,new_page;

  old_page = 0xfffff000 & *table_entry;
  if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
    *table_entry |= 2;
    invalidate();
    return;
  }
  if (!(new_page=get_free_page()))
    oom();
  if (old_page >= LOW_MEM)
    mem_map[MAP_NR(old_page)]--;
  *table_entry = new_page | 7;
  invalidate();
  copy_page(old_page,new_page);
}  

// @address 线性地址
void do_wp_page(unsigned long error_code,unsigned long address)
{
  un_wp_page((unsigned long *)
    (((address>>10) & 0xffc) + (0xfffff000 &
    *((unsigned long *) ((address>>20) &0xffc)))));
}
```
共享可以检测inode节点是否被引用至少2次来判断  
try_to_share保证了缺页异常对应的二级页表的存在  
而 Linux 0.11 启动时把内核态的低 16MB 地址空间做了恒等映射:`copy_page(old_page,new_page);`
un_wp_page发现共享没了, 就取消写保护直接返回.try_to_share发现一级页表没内容就会get一个新的二级页表, 在二级页表对应位置填上值, 使其指向被共享进程的物理页. 然后任何一个进程触发写时复制这个二级页表里对应表项就指向新的物理地址, 当然是已经被复制过的4KB物理地址  

```c
void write_verify(unsigned long address)
{
  unsigned long page;

  if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
    return;  // 直接返回, 缺页异常会处理
  page &= 0xfffff000;
  page += ((address>>10) & 0xffc);
  if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
    un_wp_page((unsigned long *) page);
  return;
}

void verify_area(void * addr,int size)
{
  unsigned long start;

  start = (unsigned long) addr;
  size += start & 0xfff; // eg. 从0x600开始读4KB实际上要跨2页, 这里就是处理这种情况的
  start &= 0xfffff000; // 地址要向下对齐到页边界
  start += get_base(current->ldt[2]);
  while (size>0) {
    size -= 4096;
    write_verify(start);
    start += 4096;
  }
}
```
我一开始有点奇怪为什么会有verify_area函数, 写时复制(COW)不是已经可以处理了吗? 原来是80386 CPU内核态, 直接把写保护无视了, 内核需要在可能的内核路径里手动触发COW. 《Linux内核完全注释V5.0》p360有相关解释  

```c
// @table_ptr 二级页表入口地址
void swap_in(unsigned long *table_ptr)
```
swap_in就相对来说很清爽, 就是获取table_ptr藏着的swap设备数据信息, 分配页表, 读完写进页表就行了  

```
@table_ptr 二级页表地址
int try_to_swap_out(unsigned long * table_ptr)
{
  if(二级页表脏){
    if(是共享页面){
      return 0;
    }
    获取一个swap位置;
    把位置填入table_ptr项里边;
    write_swap_page(swap_nr, (char *) page);
    free_page(page);
  }
  *table_ptr = 0;
  invalidate();
  free_page(page);
  return 1;
}

int swap_out(void)
{
  搜索页目录查找存在的页目录; // 16MB即最前面16个页目录不动, 这部分属于内核
repeat:
  搜索二级页表查找存在项;
  if(二级页表耗尽)
    goto repeat; // 重新搜索, 页目录回绕处理没体现
  
  if (try_to_swap_out(page_entry + (unsigned long *) pg_table))
    return 1;
  else
    goto repeat;
}
```
`page_entry + (unsigned long *) pg_table)` 是指page_entry个第二级页表项地址  
换出发生在物理内存不足还要申请空间的时候, 换进发生在检测到二级页表项存在但是存在位为0时  
页是共享的时候不换出(考虑到效率), 脏的时候换出, 不脏的时候没必要换出, 直接free就可以, 反正没改页表不需要写到交换设备    

### 磁盘底层读写路径

```c
void ll_rw_block(int rw, struct buffer_head * bh)
  make_request(major,rw,bh);
    void do_hd_request(void)
      hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
        _hd_interrupt:
          _do_hd
            static void read_intr(void)
              port_read(HD_DATA,CURRENT->buffer,256);
                end_request(1);
                  CURRENT->bh->b_uptodate = uptodate;
                  CURRENT = CURRENT->next;
```
读盘整体流程如上, 写盘雷同, 不再赘述  

```c
static void make_request(int major,int rw, struct buffer_head * bh)
{
  lock_buffer(bh);
  if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {  // 1. 
    unlock_buffer(bh);
    return;
  }
repeat:
  if (req < request) {
    sleep_on(&wait_for_request); // 2.
    goto repeat;
  }
  add_request(major+blk_dev,req);
}

static void add_request(struct blk_dev_struct * dev, struct request * req)
{
  struct request * tmp;

  req->next = NULL;
  cli(); // 1. 
  if (req->bh)
    req->bh->b_dirt = 0; // 2.
  if (!(tmp = dev->current_request)) { // 3.
    dev->current_request = req;
    sti();
    (dev->request_fn)();
    return;
  }
  for ( ; tmp->next ; tmp=tmp->next) // 4.
    if ((IN_ORDER(tmp,req) ||
        !IN_ORDER(tmp,tmp->next)) &&
        IN_ORDER(req,tmp->next))
      break;
  req->next=tmp->next;
  tmp->next=req;
  sti();
}

extern inline void end_request(int uptodate)
{
  if (CURRENT->bh) {
    CURRENT->bh->b_uptodate = uptodate;  // 成功就置为1
    unlock_buffer(CURRENT->bh);
  }
  wake_up(&CURRENT->waiting);
  wake_up(&wait_for_request);
  CURRENT->dev = -1; // 置位空
  CURRENT = CURRENT->next;
}
```
make_request:  
1. 因为有可能好几个进程写一块数据, 第一个进程已经写进去了, 后续进程由于缓冲区lock睡眠, 被唤醒就不用写了直接返回, 对读同理  
2. 在end_request被唤醒  

add_request:  
1. 全局链表在进程上下文和中断上下文都会修改
2. 排进队里就说明已经清除dirt状态了  
3. 第一个请求直接执行
4. IN_ORDER是排序规则宏; 满足规则, 或者满足50->80->120->10中120->10这个位置时, 把req插入进去  

### 字符设备
```c
_keyboard_interrupt:
  key_table(,%eax,4)
    do_self:
      put_queue // 调整指针, 放入队列
        movl _table_list,%edx    // read-queue for console
        movl head(%edx),%ecx
  void do_tty_interrupt(int tty)
    copy_to_cooked(tty_table+tty);
      GETCH(tty->read_q,c);
      PUTCH(c,tty->secondary);
      wake_up(&tty->secondary.proc_list);

int sys_read(unsigned int fd,char * buf,int count)
  return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);
    return call_addr(rw,MINOR(dev),buf,count,pos); // 注册了下层的字符读取函数
      return rw_ttyx(rw,current->tty,buf,count,pos);
        return ((rw==READ)?tty_read(minor,buf,count):
          sleep_if_empty(&tty->secondary);
          continue; // 体现了睡眠重查的思想
          GETCH(tty->secondary,c);
          put_fs_byte(c,b++);
```
从键盘读取字符流程如上, 感觉没什么好说的, 写也同理不再赘述; secondary队列的存在时要接收read_q经过termios规范处理后的字符  

### select实现
// todo  

### signal
// todo  
TASK_INTERRUPTIBLE设置为TASK_RUNNING, 并且sys_waitpid执行释放子进程资源  
sys_waitpid flag为1代表父进程找到了合适的子进程  
do_exit会有个tell_father通过信号机制来告诉父进程子进程已经退出  

## 梳理内核各类执行流

### init进程
进程1通过breada去读超级块, 并把硬盘中的根文件系统复制到内存虚拟盘区中  

进程1为了读盘去睡眠的时候, 会切到进程0执行, 正常执行而后到swich_to的cmpl %%ecx,_current发现自己就是当前任务, 直接跳到标号1. 直到进程1完成了读盘事务在切换回进程1执行, 否则当前进程0一直循环执行  

随后mount_root, 即初始化位图, 挂载根节点, 准备s_imap, s_zmap, 这里的位图块不会释放为了效率一直驻留在内存   

打开/dev/tty0, fork之后进程1会wait, 进程2把fd 0关掉, 再打开 /etc/rc, 使 /etc/rc 成为新的标准输入; 随后 execve("/bin/sh"), 继承打开的描述符, 从 /etc/rc 读命令执行; 执行完进程2就退出; 这里还会创建update进程定期向磁盘同步数据    

随后再次重建的shell程序成为真正的人机交互界面  

### 用户进程与内存管理

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 《Linux内核设计的艺术》第五章极其详细描述了多个进程创建时内核的行为  
{: .prompt-tip }
<!-- markdownlint-restore -->

### 多个进程"同时"操作一个文件

<!-- markdownlint-capture -->
<!-- markdownlint-disable -->
> 《Linux内核设计的艺术》第六章极其详细描述了多个进程"同时"操作一个文件时内核的行为 
{: .prompt-tip }
<!-- markdownlint-restore -->