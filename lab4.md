#MIT JOS Lab 4 Report

__YANG Weikun 1100012442__

_UPDATE Mon 18th Nov:_ Solved concurrency challenge using fine-grained locking

All exercises finished on Thu 10th Oct 22:30 UTC+0800  

##Ex.1 Multiprocessor support
`mmio_map_region` in `kern/pmap.c` would map page-aligned physical memory at a very high address (0xFE000000) that VM from `KERNBASE` cannot reach (over 256MB), to virtual address reserved in `[MMIOBASE, MMIOLIM)`. I called `boot_map_region` with cache-disabled, write-through and kernel-write permissions.

---
##Ex.2 Application Processor Bootstrap
By Intel's convention, on a multiprocessor platform, only the one and only bootstrap processor (BP) is started by the BIOS. We must start up other existing processors (Application processors) to utilise SMP, by emitting _'Inter Processor Interrupt'_(IPI) to each and every other processor cores. Start up code for APs is in `kern/mpentry.S`, copied to a predefined memory location before sending IPI to APs. That memory page must not appear in `page_free_list`, so `page_init()` in `kern/pmap.c` is updated to fit the change.

###Q&A
1. _Why is the macro `MPBOOTPHYS` necessary in kern/mpentry.S but not in boot/boot.S?_
    * code in `kern/mpentry.S` is linked and loaded together with all other parts of the kernel (except for the bootloader), however those instructions are copied at runtime to another physical location, so they have to discard all link/load-time assumptions. To obtain a specific physical address of a variable in relocated code & data, we need `MPBOOTPHYS`.

---
##Ex.3, 4 Per-CPU State and Initialization
First part, setup per-CPU kernel stacks in `mem_init_mp()`, `kern/pmap.c`.

* For CPU i, its kernel stack top is `KSTACKTOP - i * (KSTKSIZE + KSTKGAP)`  

        for (i = 0; i < NCPU; i++)
    		boot_map_region(kern_pgdir, KSTACKTOP - i * (KSTKSIZE + KSTKGAP) -KSTKSIZE,
    			KSTKSIZE, PADDR(percpu_kstacks[i]), PTE_W);


Second part, setup per-CPU TSS in `trap_init_percpu()`, `kern/trap.c`. 

* For CPU i, its TSS is `cpus[i].cpu_ts`, and the corresponding descriptor in GDT is `gdt[(GD_TSS0 >> 3)+i]`

Now when `jos` is started with `make qemu CPUS=4` it shows:

    6828 decimal is 15254 octal!
    Physical memory: 66556K available, base = 640K, extended = 65532K
    ...
    SMP: CPU 0 found 4 CPU(s)
    SMP: CPU 1 starting
    SMP: CPU 2 starting
    SMP: CPU 3 starting
    ...

---
##Ex.5 Locking
The _Big Kernel Lock_ is obtained at defined places:

* In `i386_init()` just before waking up other CPUs. This is to prevent other CPUs from acquiring the lock before bootstrap CPU finishes initialisation. This could happen earlier.
* In `mp_main()`, where the APs are about to enter the scheduler for the first time.
* In `trap()`, after user processes are trapped into kernel mode.

Those 3 points all indicate the beginning of some _serious kernel work_, that operates sensitive data structures that need mutual-exclusion protection.

BKL is released just before `env_pop_tf`.
###Q&A
1. _Why do we still need separate kernel stacks for each CPU despite the 'Big Kernel Lock'?_
    * Because, when a interrupt occurs in user mode, the CPU will push `EIP`, `CS` etc. on stack by hardware, without acquiring a lock. In case another CPU is running in kernel mode, the shared stack will be corrupted.
  
--- 
##Challenge: Concurrency in Kernel
The Linux Kernel, to support SMP, used _Big Kernel Lock_ to serialise, until version `2.6.39` in 2011. What we are doing here in `joe` is quite similar. The kernel acquires a giant lock to prevent all forms of concurrency, so no modification is necessary for any of the kernel data structures. Now, I will replace the _Big Kernel Lock_ mechanism with fine-grained locking that only lock little parts of the kernel, to embrace concurrency in kernel. It proved to be something actually _challenging_.

First, consider which parts of the current kernel will fail when operated concurrently (copied from the hint provided by MIT), and how exactly:

1. __Page Allocator__: `page_free_list` must be handled atomically. 
    * Scenario 1A: kernel on CPU0 calls `page_alloc`, so does kernel on CPU1, CPU2 etc. They all believe `page_free_list` is not NULL, then took the first page on the list, then moved forward `page_free_list` by one. Now, they've allocated a page -- but they are holding the same page. Suppose `page_alloc`ed pages are inserted into different user processes, now those processes mystically shares a physical page they thought private.  
    
            if ((page = page_free_list) == NULL)
    		    goto page_alloc_end;
        	page_free_list = page_free_list->pp_link;
	* Scenario 1B: kernel on CPU0 calls `page_decref` wanting to get rid of a page. This page's `pp_ref` is `1`, so it should be put back on the free list. `page_decref` is actually a `read-modify-(test)-and-write` process. Suppose CPU0 has not commited the change then kernel on CPU1 comes in and tries to map this very page to somewhere else, CPU1 believes `pp_ref` is still one so it proceeds with mapping, while CPU0 later puts the page on free list.
	        
	        if (-- pp->pp_ref == 0)
	            ...
	 * There exists plenty more horrible scenarios.
2. __Console Driver__: The console driver (actually the CGA and Serial buffer) must be accessed one at a time.
    * Scenario 2A: Two kernel instances both call `cprintf(...)` to deliver some information. Since `cprintf` outputs one character at a time, we might see two messages intermixed.
    * Scenario 2B: Two kernel instances both call a series of `cons_getc()`. A complete sentence may be split into pieces when it hits those kernels' line buffers.
    
3. __Scheduler__: The scheduler considers the status of a process when deciding which one to run, it also posts modification on the status. Here's how it might fail.
    * Scenario 3A: Two schedulers run at the same time. The first finds a process to load and run, but just before it could mark it `running`, the other scheduler picks the same process. Later this process may run simultaneously on two CPUs.
    * Scenario 3B: The parent process calls `sys_destroy` to kill one of its child, just before the child is marked `dying` or `free`, a scheduler picks this child process to run. Sadly `env_destroy` continues to free memory of a running process.
    * Scenario 3C: One process makes a system call. A scheduler managed to run the process before the completion of that system call.
    * This part of the kernel could go terribly wrong.
4. __IPC states__: When a process wants to receive a message, it may receive one and only one.
    * Scenario 4A: One process calls `sys_ipc_recv`, the kernel marks it receivable. Then many other processes call `sys_ipc_try_send` to the receiving process. They all believe that process wants their message, so memory could be mapped again and again, while the message value may get overwritten many times.

Apparently I'm not the first one to realise locks are used to lock __data__, not __code__. This is very important as such a tiny little kernel like `jos` may present very complicated code-paths, especially on multiple processors. We must focus on concurrency-sensitive data structures to avoid errors.

From those scenarios above, we conclude that there are so many data structures that must be protected:

1. `page_free_list`: since dereferencing a pointer and change it's value is quite difficult to be encapsulated as an atomic operation, I use a spin lock (`pm_lock`) to protect it. `pm_lock` is used in two places: `page_alloc()` and `page_free()`
2. `pp_ref` of every physical page: `pp_ref` is declared as an atomic variable, I used `atomic_inc` and `atomic_dec_test` to operate on it.
3. CGA buffer and Serial buffer: since we want calls to `cprintf` to be atomic (which means generally a sentence is not split into pieces), I used spin lock `cons_write_lock` inside `vprintfmt`. Note that `vprintfmt` is used in both kernel and user mode, only kernel mode requires locking (because user mode `cprintf` is buffered, and translates to kernel mode `vprintfmt`).
4. Keyboard and Serial input: the provided test programs for this lab did not use much of the input utility, and locking the input driver will require an input line buffer, so I did not implement locking here.
5. IPC states: IPC states are modified in two places, `sys_ipc_recv` and `sys_ipc_try_send`. Each user process has its own `env_ipc_lock`.
6. All other states of every user process `envs`: To make life easier, I use one big spin lock to protect the whole `envs` array. Protected data structures include: environments free list, environment status etc. And one atomic variable is used for every process to indicate whether they are in middle of an system call. In `env_run`, we must wait until the system call is finished before giving CPU to it.

It all seems rather straight forward after my explanation, but the debugging process was not an pleasing experience. 

1. Operating Systems are generally considered un-debuggable, so `cprintf` is pretty much every thing you get. Running `jos` on `QEMU` that can talk to `GDB` is such a treasure, but still it's nothing like user space debugging. I really can't imagine how people are debugging the Linux kernel.
2. Reproducibility is an serious issue when timer interrupts comes in randomly, and `QEMU`'s SMP is not so consistent.
3. Concurrent programs are hard to debug, inheritedly.

More on concurrency and synchronisation. For the sake of simplicity we used only spin locks (which are not recursive) and atomic variables (their implementation is strongly associated with architecture). And it's very nice that `jos`'s kernel is not preemptive. 

__Important:__
The `primes` and `pingpong` programs are perfect tests. Please do not be surprised with occasional warning messages coming out of the console, or very long periods of stall. The scheduler of `QEMU` isn't very stable. It sometimes prefers to run the CPU that is waiting for a lock, not the one that's holding the lock. And it seems that the emulated cache or memory is somehow inconsistent. Or maybe it's just my own fault.

I consulted [Linux Cross Reference](http://lxr.free-electrons.com/) for implementation of atomic operations. And _Linux Kernel Development, 3rd edition by Robert Love_ for general synchronisation in OS kernels.

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

##Ex.8 Setting the Page Fault Handler
Implement `int sys_env_set_pgfault_upcall(envid_t envid, void *func)` to set the user mode page fault handler. The entry point of the handler is stored in `env->env_pgfault_upcall`.

##Ex.9 Invoking the User Page Fault Handler
To invoke the user page fault handler we must modify `page_fault_handler` in `kern/trap.c`. This function first handles kernel mode page fault. It proceeds with calling the user mode page fault handler only when all of the following conditions are met:

1. The user program has already registered a page fault up call by `sys_env_set_pgfault_upcall`
2. The user program has already allocated a page of memory for the exception stack, from VA `UXSTACKTOP-PGSIZE` to `UXSTACKTOP-1`, and the mapping permits user write.
3. If the page fault happened while the user was processing a previous page fault, there must be enough space for the new handler's arguments on the exception stack. The stack must not overflow when new arguments are pushed.

Otherwise, our kernel destroys this faulting user program.

In my implementation, I switched `CR3` to the page directory of the current user program to push arguments (a `struct UTrapframe`) on it's exception stack. Also if the trap-time user stack is the exception stack itself, another 4 bytes of space is reserved on stack before the arguments.

---

##Ex.10, 11 User-mode Page Fault Entry Point
The `_pgfault_upcall` routine in `lib/pfentry.S` is the one of the most sophisticated assembly code I've written in this lab. After the user defined `_pgfault_handler` returns, we must restore all general purpose registers, including stack pointer, instruction pointer and flags register to their trap-time state. The only way of transferring both control(`%eip`) and stack(`%esp`) back to the site that caused the page fault at the same time, is only achievable by pushing the trap-time `%eip` on trap-time stack, then call `ret` to hand over control. I would like to quote a bit of the code for clarity:

    _pgfault_upcall:
	pushl %esp        // function argument: pointer to UTF
	movl _pgfault_handler, %eax
	call *%eax        // doing this because near jump does not support imm32
	addl $4, %esp     // pop function argument
	addl $8, %esp       // pop utf_fault_va and utf_err off the stack
	movl 32(%esp), %eax // load the trap-time %eip to %eax
	subl $4, 40(%esp)   // decrease the trap-time %esp by 4, load to %ebx
	// this is neccesary because i386 does not support `write to pointor in memory`
	movl 40(%esp), %ebx
	movl %eax, (%ebx)   // store the trap-time %eip to (adjusted) trap-time stacktop
	popal        // restore the trap-time registers.
	addl $4, %esp	    // pop the eip
	popfl        // restore eflags from the stack.
	popl %esp    // switch back to the adjusted trap-time stack.
	ret          // return to re-execute the instruction that faulted.

`set_pgfault_handler` is much simpler, it allocates the exception stack if is called first, registers the user mode page fault handler using `sys_env_set_pgfault_upcall`.

Now my jos succeeds with `user/faultread`, `user/faultdie`, `user/faultalloc`, and `user/faultallocbad`.

---

##Ex.12 Implementing Copy-on-Write Fork
Things to do in `fork()` to support COW:

1. Register parent process's page fault handler: `pgfault()` (later implemented).
2. Call `sys_exofork()` to create a new Env. Depending on the return value, code path splits:
    * `sys_exofork()`'s return value is 0, we are the child process. Actually this system call will not return immediately in child like normal linux `fork` that might introduce race conditions, until the parent process explicitly sets the child's status, and the kernel reschedules. After `sys_exofork` returns we must fix `thisenv` to point to the right Env, since the old `thisenv` is inherited from the parent.
    * `sys_exofork()`'s return value is a positive integer, which means that the system call succeeded and we are now in the parent process. Quite a lot of things to be done for the child process:
        a. First, scan through the parent process's virtual address space from `0` to `UXSTACKTOP-PGSIZE`, copy every existing mapping using `duppage()` (implemented later).
        b. Allocate child's exception stack, by `sys_page_alloc`.
        c. Register a page fault handler for the child using `sys_env_set_pgfault_upcall`. Here we cannot use the user mode function `set_pgfault_handler` because it can only do things for the current process.
        d. Make the child process runnable by `sys_env_set_status`.

        The order of those things is rather important: we must register a page fault handler before the child is marked runnable. Otherwise if later we use a preemptive multiprocessing strategy, the child might start running on copy-on-write memory without a page fault handler.
    * sys_exofork()'s return value is a negative integer, which indicates failure.

    In all three cases, I chose not to `panic` on exceptional conditions, just delegate the return value to the caller of `fork` instead.

Now we move on to the COW mapping-transferring function `duppage()`:

* For a virtual page that is originally mapped writable or copy-on-write, we map the page in both parent and child with COW permissions.
    * Why is it necessary to map in the parent those COW pages again?
        * no idea, not necessary according to my tests.
* For a virtual page that is originally read-only, we map the page in the child read-only as well.
* In case of any system call failure, `duppage()` will not panic but return the error code to the caller function.

Next, the core of COW : page fault handler `pgfault()`

* `pgfault` first checks that the fault was caused by write to COW location. Otherwise it raise a `panic`. In a more realistic scene, `pgfault()` could delegate the 'otherwise' to another general page fault handler.
* Then, `pgfault` allocate a new page at `PFTEMP`, map it to the faulting location (rounded down to nearest page boundary) with user-write permission, then undo the mapping at `PFTEMP`.

Now our COW-fork mechanism is rather complete, moving on to testing `user/forktree` on single CPU (I modified the fork depth to 2):

    [00000000] new env 00001000
    1000: I am ''
    [00001000] new env 00001001
    [00001000] new env 00001002
    [00001000] exiting gracefully
    [00001000] free env 00001000
    1001: I am '0'
    [00001001] new env 00002000
    [00001001] new env 00001003
    [00001001] exiting gracefully
    [00001001] free env 00001001
    2000: I am '00'
    [00002000] exiting gracefully
    [00002000] free env 00002000
    1002: I am '1'
    [00001002] new env 00003000
    [00001002] new env 00002001
    [00001002] exiting gracefully
    [00001002] free env 00001002
    3000: I am '10'
    [00003000] exiting gracefully
    [00003000] free env 00003000
    2001: I am '11'
    [00002001] exiting gracefully
    [00002001] free env 00002001
    1003: I am '01'
    [00001003] exiting gracefully
    [00001003] free env 00001003
    No runnable environments in the system!
    
Part B of lab4 is complete.

---
##Ex.13, 14 Clock Interrupts and Preemption
IDT entries are already fully setup in the previous lab, using `generate_traps.py`. To allow user programs to accept interrupts, `FL_IF` flag in `flags` must be set in `env_alloc()`. Then when a clock interrupt `IRQ_OFFSET + IRQ_TIME` enters the trap dispatcher, we should first acknowledge it by `lapic_eoi()`, then call the scheduler `schedule_yield()` to enable preemption.

---
##Ex.15 Implementing IPC
Having all the previous work in help, IPC becomes pretty easy to implement. Our IPC mechanism supports the sender to send a 4-byte word to the receiver, optionally sharing a page of memory between. Four of the core functions:

1. In kernel space (system calls):
    a. `int sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)` this function will try to locate a Env with `envid`, send the value to it only if the selected Env bas called `sys_ipc_recv` already to receive the value. If `srcva` and `perm` are meaningful, and that the receiver allowed shared mapping to be made, a page of memory from the sender at address `srcva` would be mapped to `dstva` of the receiver. This system call does not wait for the receiver to be ready, so the library wrapper must retry infinitely waiting for the receiver. This function must change the receiver to runnable state, and clear the `env_ipc_recving` flag after the message was sent.
    b. `int sys_ipc_recv(void *dstva)` A processing expecting a message from another process will call this function, to mark itself ready. This function does not return until the message is received.
2. In user space (library calls):
    a. `void ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)` a library function that repeatedly calls `sys_ipc_try_send` to send a message to another process. This function would `panic` if the provided arguments are illegal.
    b. `int32_t ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)` this wrapper calls `sys_ipc_recv`, upon return, it fills `from_env_store` and `perm_store` if necessary, then return the received 4-byte message to the caller.    
    
---

##Final testing:
Now all exercises are complete, moving on to testing:

1. My output for `user/badhandler` and `user/evilhandler` tests was different from the expected. After carefully reading this piece of comment in `kern/trap.c`:

        // Note that the grade script assumes you will first check for the page
        // fault upcall and print the "user fault va" message below if there is
        // none.  The remaining three checks can be combined into a single test.
So in `page_fault_handler`, we must check both the existence and validity of user's page fault handler (using `user_mem_assert`) before other things, to make the grading script happy.
2. `user/primes` not behaving correctly. Later I realised this was caused by a faulty `sys_ipc_try_send` that does not clear the receiver's `env_ipc_recving` flag after successfully transferring the message. When I fixed this, the maximum prime number `user/primes` calculated was `8147`, by the final process, afterwards `fork` failed because the OS ran out of space for `Env`. 

---

**Now All exercises of Lab4 are done, all tests passed. (auto-grade does not work on OS X, tested manually)**