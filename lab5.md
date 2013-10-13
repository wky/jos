#MIT 6.828 JOS Lab5

**YANG Weikun 1100012442**

14th Oct 2013 02:00 UTC+0800

##Getting Started

After merging with `origin/lab5`, `GNUmakefile` uses native gcc to compile something for the disk image. The gcc on my system is `x86_64-apple-darwin13.0.0-gcc`.

##Ex.1 Disk Access
Following the monolithic kernel design, we are moving the file system out side the OS. Disk access are all handled in a special user process with type `ENV_TYPE_FS`.  We use polling I/O to further reduce the complexity.

`IOPL` bits in `EFLAGS` register control device I/O privilege. So the FS process will be the only process initiated with such privilege.

###Q&A
1. _'Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?'_
    * Nope, I didn't need to do anything else. When an interrupt occurs the `EFLAGS` register is saved automatically by hardware. When the kernel returns to user programs `EFLAGS` is restored.
    
---
##Ex.2 The Block Cache
Our block cache strategy can't be simpler: we use 3GB of memory space from `DISKMAP`, to map the entire disk. Access to disk block `i` will try to access memory location `DISKMAP + i * BLKSIZE`. If the block does not exist in memory a page fault will occur, then our page fault handler will allocate space and read the actual disk contents in memory.

In this exercise I encountered several strange problems, one of which is that `ROUNDDOWN` causes illegal memory access when code is compiled with `-O0`. Never figured out why, so I didn't use `ROUNDDOWN` anymore.

---
##Ex.3 Spawning Processes
To implement a user mode `spawn` (equivalent to UNIX `fork + exec`), we must provide an interface for user programs to modify the trap frame of another user program, the system call `sys_env_set_trapframe`. In this system call, we first validate the pointer from user, then make sure that the modified trap frame has:

1. `FL_IF` on to receive interrupts.
2. `FL_IOPL_MASK` off (to deny I/O operations).
3. `CPL` of 3 so the program will run in user mode.

---
##Ex.4 Sharing library state across fork and spawn
Following instruction from MIT, we must find pages marked with `PTE_SHARE` and map them accordingly in both `fork` and `spawn`.

---
##Ex.5 The keyboard interface
Input from QEMU window will be sent as keyboard interrupts with number `IRQ_OFFSET+IRQ_KBD`, handled by `kbd_intr`; Input from terminal window (of the host) will be sent as serial interrupts with number `IRQ_OFFSET+IRQ_SERIAL`, handled by `serial_intr`. `trap_dispatch` is modified to suit the change.

###Q&A
1. _'How long approximately did it take you to do this lab?'_
    * A bit more than 3 hours (without doing the challenges). This seems the easiest lab in all 5.
2. _'Any suggestions?'_
    * All 5 labs are great! Wonder if 64-bit `jos` is coming next year?