/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_SCHED_H
#define JOS_KERN_SCHED_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#define BALANCE_CNT 1
extern int balance_cnt;
// This function does not return.
void sched_yield(void) __attribute__((noreturn));
void append_env(struct CpuInfo *cpu, struct Env *e);
int delete_env(struct CpuInfo *cpu, struct Env *e);
#endif	// !JOS_KERN_SCHED_H
