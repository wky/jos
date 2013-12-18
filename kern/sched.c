#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/syscall.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/sched.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

int balance_cnt;

void
append_env(struct CpuInfo *cpu, struct Env *e){
	e->env_link = cpu->env_list;
	cpu->env_list = e;
	cpu->nenv ++;
}

int
delete_env(struct CpuInfo *cpu, struct Env *e){
	if (e == cpu->env_list){
		cpu->nenv --;
		cpu->env_list = e->env_link;
		return 1;
	}
	struct Env *begin = cpu->env_list;
	while (begin->env_link){
		if (begin->env_link == e) break;
		begin = begin->env_link;
	}
	if (begin->env_link){
		begin->env_link = e->env_link;
		cpu->nenv --;
		return 1;
	}
	return 0;
}

void
print_env(struct CpuInfo * cpu)
{
	struct Env *tmp = cpu->env_list;
	while (tmp){
		cprintf("%x ", tmp->env_id);
		tmp = tmp->env_link;
	}
	cprintf("\n");
}

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *end, *begin;

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
/*
	begin = curenv? curenv + 1:envs;
	if (begin == envs + NENV)
		begin = envs;
	idle = begin;
	spin_lock(&env_lock);
	while (idle != envs + NENV){
		if (idle->env_status == ENV_RUNNABLE)
			goto sched_yield_run;
		idle ++;
	}
	idle = envs;
	while (idle != begin){
		if (idle->env_status == ENV_RUNNABLE)
			goto sched_yield_run;
		idle ++;
	}
	if (curenv && curenv->env_status == ENV_RUNNING) idle = curenv;
	else sched_halt();
sched_yield_run:
	env_run(idle);
*/
	spin_lock(&env_lock);
	if (cpunum() == 0 && (balance_cnt % BALANCE_CNT) == 0){ // master
		int i = 0, total = 0, max;
		struct Env *list = NULL, *tmp = NULL;
		// cprintf("load balance:\n");
		for (; i < ncpu; i++){
			// cprintf("cpu%d: %d ", i, cpus[i].nenv);
			// print_env(cpus + i);
			total += cpus[i].nenv;
		}
		max = (total+ncpu-1)/ncpu;
		for (i = 0; i < ncpu; i++){
			while (cpus[i].nenv > max){
				tmp = cpus[i].env_list;
				delete_env(cpus + i, tmp);
				tmp->env_link = list;
				list = tmp;
			}
		}
		// cprintf("after load balance:\n");
		for (i = 0; i < ncpu; i++){
			while (cpus[i].nenv < max){
				if (list){
					tmp = list;
					list = list->env_link;
					append_env(cpus + i, tmp);
				} else break;
			}
			// cprintf("cpu%d: %d ", i, cpus[i].nenv);
			// print_env(cpus + i);
		}
		spin_unlock(&env_lock);
		spin_lock(&env_lock);
	}
	end = begin = thiscpu->env_list;
	while (end && end->env_link) end = end->env_link;
	while (begin != NULL){
		if (begin->env_status == ENV_RUNNABLE) break;
		begin = begin->env_link;
	}
	if (!begin && curenv && curenv->env_status == ENV_RUNNING) begin = curenv;
	if (begin){
		end->env_link = thiscpu->env_list;
		thiscpu->env_list = begin->env_link;
		begin->env_link = NULL;
		// print_env(thiscpu);
		// cprintf("picking %x\n", begin->env_id);
		env_run(begin);
	}
	sched_halt();
	panic("sched");
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;
	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	/*
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
			cprintf("No runnable environments in the system!\n");
			while (1)
					monitor(NULL);
	}*/
	spin_unlock(&env_lock);
	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	// cprintf("cpu[%d] halted.\n", cpunum());
	xchg(&thiscpu->cpu_status, CPU_HALTED);

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

