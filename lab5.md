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

##Challenge: File system with write access

###JOS filesystem overview
1. __File Operations__(for user programs) `open`, `close`, `seek`, `read`, `write`, `stat` and other functions are visible to user programs, to accomplish file operations using `fd`(file descriptors). Most of those functions are delegates to device specific handlers. They are defined in `fd.c` and `file.c`. There are many other functions to manage a process's `fd`s.
2. __Device Abstraction__ Files, Pipes, Consoles are the three devices in JOS. Each type of device has its own handlers for various operations. `devfile_read`, `devfile_write` etc. are handlers of File device (in `file.c`). Those functions invoke IPC to the file server.
3. __IPC to File Server__ Upon receiving a request from a user process, the file server will interpret the requested job, dispatch the actually work to the next level. The file server is in `serv.c`.
4. __File Operations__ This level will walk through directories to open a file, locate blocks for read/write. This part is in `fs.c`
5. __Block Cache__ The entire disk is cached in the file server: `[DISKMAP, DISKMAP+DISKSIZE)`. The file server will try the memory for read/write of blocks, the block cache will load blocks into memory when necessary. Write-back and eviction is added to enable a writable filesystem.
6. __On Disk Structure__ On the disk, block 0 is the MBR. Block 1 is the superblock, containing the size of the disk and the root directory. starting from block 2 lies the bitmap, to keep track of allocated blocks and free blocks. Then comes blocks used for meta-data and contents of files.
7. __IDE Driver__ The driver issues IDE commands to read/write physical blocks.

###Changes made to support writable filesystem.
1. __Device level operations__ handlers for write and truncate are added (`devfile_write` and `devfile_trunc`), both are simple wrappers of corresponding filesystem IPC calls.
2. __File Server__ the file server now may `open` a file with `O_CREAT`, `O_TRUNC`, `O_WRONLY` and `O_RDWR` options. All FS requests are handled appropriately.
3. __File Operations__ in `fs.c`:
    * `alloc_blk`: find a free block in the bitmap
    * `free_blk`: free a disk block
    * `block_write_back`: write back changes to disk (if any), and un-map the corresponding memory page to release memory(the superblock is never unmapped).
    * `file_block_walk`: find the pointer to the disk block that holds contents of a block in file. may allocate an indirect block if asked.
    * `file_get_block`: find the actually disk block of a file, allocate one if none exists.
    * `file_create`: create a new file or directory. allocate a new directory entry and create a file.
    * `file_write`: write contents of buffer to appropriate location in file. File size may grow if writing past EOF.
    * `file_set_size`: truncate (delete content blocks) or extend (allocate content blocks) the size of a file.
    * `file_flush`: commit all changes on a file to disk.
    * `fs_sync`: commit all changes on the filesystem to disk.
4. __Block Cache__ when the block cache is out-of-memory, the file server will call `fs_sync` to free memory.

###Results
The filesystem in JOS is now able to create new files and directories, write data to files, truncate or extend a file. The simplest test would be:

    $cat > newfile  
    blah blah blah
    ...
    
    $ls
    ...
    newfile
    ...
    
    $cat newfile
    blah blah blah
    ...




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
    * Writable filesystem challenge took some time. I had to understand the whole filesystem implementation, then make appropriate changes.
2. _'Any suggestions?'_
    * All 5 labs are great! Wonder if 64-bit `jos` is coming next year?