#MIT JOS Lab 4 Report

__YANG Weikun 1100012442__

##Ex.1 Multiprocessor support
`mmio_map_region` in `kern/pmap.c` would map page-aligned physical memory at a very high address (0xFE000000) that VM from `KERNBASE` cannot reach (over 256MB), to virtual address reserved in `[MMIOBASE, MMIOLIM)`. I called `boot_map_region` with cache-disabled, write-through and kernel-write permissions.

---
##Ex.2 Application Processor Bootstrap
By Intel's convention, on a multiprocessor platform, only the one and only bootstrap processor (BP) is started by the BIOS. We must start up other existing processors (Application processors) to utilise SMP, by emitting _'Inter Processor Interrupt'_(IPI) to each and every other processor cores. Start up code for APs is in `kern/mpentry.S`, copied to a predefined memory location before sending IPI to APs. That memory page must not appear in `page_free_list`, so `page_init()` in `kern/pmap.c` is updated to fit the change.

###Q&A
1. _Why is the macro `MPBOOTPHYS` necessary in kern/mpentry.S but not in boot/boot.S?_
    * code in `kern/mpentry.S` is linked and loaded together with all other parts of the kernel (except for the bootloader), however those instructions are copied at runtime to another physical location, so they have to discard all link/load-time assumptions. To obtain a specific physical address of a variable in relocated code & data, we need `MPBOOTPHYS`.

---
##Ex.3&4 Per-CPU State and Initialization
First part, setup per-CPU kernel stacks in `mem_init_mp()`, `kern/pmap.c`.

* For CPU i, its kernel stack top is `KSTACKTOP - i * (KSTKSIZE + KSTKGAP)`

Second part, setup per-CPU TSS in `trap_init_percpu()`, `kern/trap.c`. 

* For CPU i, its TSS is `cpus[i].cpu_ts`, and the corresponding descriptor in GDT is `gdt[(GD_TSS0 >> 3)+i]`

Now when `jos` is started with `make qemu CPUS=4` it shows:

    6828 decimal is 15254 octal!
    Physical memory: 66556K available, base = 640K, extended = 65532K
    check_page_alloc() succeeded!
    check_page() succeeded!
    check_kern_pgdir() succeeded!
    check_page_installed_pgdir() succeeded!
    SMP: CPU 0 found 4 CPU(s)
    enabled interrupts: 1 2
    SMP: CPU 1 starting
    SMP: CPU 2 starting
    SMP: CPU 3 starting
    ...

---
##Ex.5 Locking
The _Big Kernel Lock_ is obtained at defined places, and release just before the `asm volatile(...)` statement in `env_pop_tf`.
###Q&A
1. _Why do we still need separate kernel stacks for each CPU despite the 'Big Kernel Lock'?_
    * Because, when a interrupt occurs in user mode, the CPU will push `EIP`, `CS` etc. on stack by hardware, without acquiring a lock. Incase another CPU is running in kernel mode, the shared stack will be corrupted.
  
--- 
##Ex.6 Round-Robin Scheduling
