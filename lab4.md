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
    * Because, when a interrupt occurs in user mode, the CPU will push `EIP`, `CS` etc. on stack by hardware, without acquiring a lock. In case another CPU is running in kernel mode, the shared stack will be corrupted.
  
--- 
##Ex.6 Round-Robin Scheduling
Before making this part of the lab work nicely, I had to write a fake timer interrupt handler in `trap_dispatch()`. Somehow timer interrupts are enabled.

For function `schedule_yield()`, we must check circularly in the `envs` array for the first `Env` with status of `ENV_RUNNABLE`, starting from `curenv` if it exists, or beginning of the array if not. If no candidate was found then `curenv` may be scheduled again. Then we call `env_run()` to drop to selected user program, or drop to `schedule_halt()` that shuts down the CPU. Next we need to dispatch `sys_yield` system call.

This is what I get on a 4-CPU system running 6 `user_yield` instances:

    [00000000] new env 00001000
    ...
    [00000000] new env 00001005
    Hello, I am environment 00001000.
    Hello, I am environment 00001002.
    ...
    Back in environment 00001000, iteration 0.
    Back in environment 00001002, iteration 0.
    Hello, I am environment 00001005.
    Back in environment 00001001, iteration 0.
    ...
    Back in environment 00001001, iteration 4.
    Back in environment 00001004, iteration 3.
    All done in environment 00001000.
    ...
    [00001000] exiting gracefully
    [00001000] free env 00001000
    ...
    Back in environment 00001005, iteration 3.
    ...
    All done in environment 00001004.
    Back in environment 00001005, iteration 4.
    [00001003] exiting gracefully
    ...   
    No runnable environments in the system!
###Q&A
1. _Why can the pointer `e` be dereferenced both before and after the addressing switch?_
    * Most of the pointers used in kernel points to somewhere above `KERNBASE` (virtual mem), recall that all user memory spaces are identical to kernel memory space for VA above `KERNBASE`, therefore it is OK to address something in kernel space using a user environment's page directory.
2. _Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?_
    * Such context switch, in our current Round-Robin scheduling, may happen only when:
        * A process gives itself up to the kernel by making the `yield` system call.
        * An interrupt of any other kind occurs. (For some cases the interrupted process may not resume)
    * In both cases, process states are saved by our interrupt mechanism, details explained in the previous lab.
    
---
##Ex.7 System Calls for Environment Creation
There are 5 new system calls to be implemented, to support dumb fork (copy-on initilize).

1. `envid_t sys_exofork(void)` create a new `Env` using `env_alloc()`, set its status to `ENV_NOT_RUNNABLE` and copy the trap frame from the calling `Env` to this new `Env`. To later return `0` to the child, we must set `eax` register in the trap frame explicitly.
2. `int sys_env_set_status(envid_t envid, int status)` find the `Env` with `envid`, and set it's status to `ENV_NOT_RUNNABLE` or `ENV_RUNNABLE`.
3. `int sys_page_alloc(envid_t envid, void *va, int perm)` this one is slightly more complex. We must ensure the existence of `envid` and the validity of `va` and `perm`. Then we allocate a physical page and insert it into the `Env`'s VM.
4. `int sys_page_map(envid_t srcenvid, void *srcva, envid_t dstenvid, void *dstva, int perm)` this is a syscall that copies memory mappings between different `Env`s. Like the previous one, but a lot more to verify. `page_lookup` is called to find the original mapping, then `page_insert` is called to setup the new mapping to the same physical location.
5. `int sys_page_unmap(envid_t envid, void *va)` We use `page_remove` to do the job.

Up to now all exercises of part A of lab4 is complete. My version of `jos` is now able to run `user/dumbfork` correctly (on 2 CPUs):

    [00000000] new env 00001000
    [00001000] new env 00001001
    0: I am the parent!
    0: I am the child!
    ...
    ...
    9: I am the parent!
    9: I am the child!
    [00001000] exiting gracefully
    [00001000] free env 00001000
    10: I am the child!
    ...
    19: I am the child!
    [00001001] exiting gracefully
    [00001001] free env 00001001
    No runnable environments in the system!
    ...
---