#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/syscall.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle, *begin;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
	begin = curenv? curenv + 1:envs;
	if (begin == envs + NENV)
		begin = envs;
	idle = begin;
	spin_lock(&env_lock);
	while (idle != envs + NENV){
		if (idle->env_status == ENV_RUNNABLE) goto sched_yield_run;
		idle ++;
	}
	idle = envs;
	while (idle != begin){
		if (idle->env_status == ENV_RUNNABLE) goto sched_yield_run;
		idle ++;
	}
	if (curenv && curenv->env_status == ENV_RUNNING) idle = curenv;
	else { // sched_halt never returns
		spin_unlock(&env_lock);
		sched_halt();
	}
sched_yield_run:
	if (idle->env_tf.tf_regs.reg_esi == SYS_ipc_recv && idle->env_tf.tf_regs.reg_eax == SYS_ipc_recv){
		cprintf("caught in sched_yield!\n");
	}
	env_run(idle);
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	if (env_lock.locked && env_lock.cpu == thiscpu)
		panic("cpu[%d] about to halt but still holding env_lock", cpunum());
	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	// cprintf("CPU %d about to halt!\n", cpunum());
	// unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		"sti\n"
		"hlt\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

