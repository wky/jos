// System call stubs.

#include <inc/syscall.h>
#include <inc/lib.h>
#include <kern/kdebug.h>

static void
mon_backtrace()
{
	// base pointer
	unsigned int *bp;
	cprintf("Stack backtrace:\n");
	// load %ebp to bp
	__asm__ ("movl %%ebp, %0":"=r"(bp) :);
	// entry.S set the first %ebp to be zero
	while (bp != 0){
		// display ebp, return address, and parameters on stack
		cprintf("  ebp \x1b[9m%08x\x1b[0m  eip \x1b[9m%08x\x1b[0m\n",
			bp, bp[1]);
		// bp points to the previous stack frame's saved %ebp
		bp = (unsigned int*)(bp[0]);
	}
	panic("...");
}

static inline int32_t
syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.
	uint32_t runs = 0, status;
	// if (num != 2 && thisenv){
	// 	runs = *(volatile int*)(&(thisenv->env_runs));
	// 	status = *(volatile int*)(&(thisenv->env_status));
	// 	if (status != ENV_RUNNING){
	// 		cprintf("bad things happend (before syscall %d) env_status=%d not right...%08x...\n", num, thisenv->env_status, thisenv->env_id);
	// 		mon_backtrace();
	// 	}
	// }
	asm volatile("int %1\n"
		: "=a" (ret)
		: "i" (T_SYSCALL),
		  "a" (num),
		  "d" (a1),
		  "c" (a2),
		  "b" (a3),
		  "D" (a4),
		  "S" (a5)
		: "cc", "memory");

	// if (num != 2 && thisenv){
	// 	status = *(volatile int*)(&(thisenv->env_status));
	// 	// if (*(volatile int*)(&(thisenv->env_runs)) <= runs)
	// 	// 	cprintf("bad things happend... env_runs=%d not right...%08x...\n", thisenv->env_runs, thisenv->env_id);
	// 	if (status != ENV_RUNNING){
	// 		cprintf("bad things happend (after syscall %d) env_status=%d not right...%08x...\n", num, thisenv->env_status, thisenv->env_id);
	// 		mon_backtrace();
	// 	}
	// }
	if(check && ret > 0)
		panic("syscall %d returned %d (> 0)", num, ret);

	return ret;
}

void
sys_cputs(const char *s, size_t len)
{
	syscall(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}

int
sys_cgetc(void)
{
	return syscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
}

int
sys_env_destroy(envid_t envid)
{
	return syscall(SYS_env_destroy, 1, envid, 0, 0, 0, 0);
}

envid_t
sys_getenvid(void)
{
	 return syscall(SYS_getenvid, 0, 0, 0, 0, 0, 0);
}

void
sys_yield(void)
{
	syscall(SYS_yield, 0, 0, 0, 0, 0, 0);
}

int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	return syscall(SYS_page_alloc, 1, envid, (uint32_t) va, perm, 0, 0);
}

int
sys_page_map(envid_t srcenv, void *srcva, envid_t dstenv, void *dstva, int perm)
{
	return syscall(SYS_page_map, 1, srcenv, (uint32_t) srcva, dstenv, (uint32_t) dstva, perm);
}

int
sys_page_unmap(envid_t envid, void *va)
{
	return syscall(SYS_page_unmap, 1, envid, (uint32_t) va, 0, 0, 0);
}

// sys_exofork is inlined in lib.h

int
sys_env_set_status(envid_t envid, int status)
{
	return syscall(SYS_env_set_status, 1, envid, status, 0, 0, 0);
}

int
sys_env_set_pgfault_upcall(envid_t envid, void *upcall)
{
	return syscall(SYS_env_set_pgfault_upcall, 1, envid, (uint32_t) upcall, 0, 0, 0);
}

int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, int perm)
{
	return syscall(SYS_ipc_try_send, 0, envid, value, (uint32_t) srcva, perm, 0);
}

int
sys_ipc_recv(void *dstva)
{
	return syscall(SYS_ipc_recv, 1, (uint32_t)dstva, 0, 0, 0, SYS_ipc_recv);
}

