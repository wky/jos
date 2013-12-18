#ifndef JOS_INC_SYSCALL_H
#define JOS_INC_SYSCALL_H

/* system call numbers */
enum {
	SYS_cputs = 0,
	SYS_cgetc = 1,
	SYS_getenvid = 2,
	SYS_env_destroy = 3,
	SYS_page_alloc = 4,
	SYS_page_map = 5,
	SYS_page_unmap = 6,
	SYS_exofork = 7,
	SYS_env_set_status = 8,
	SYS_env_set_pgfault_upcall = 9,
	SYS_yield = 10,
	SYS_ipc_try_send = 11,
	SYS_ipc_recv = 12,
	SYS_env_set_trapframe = 13,
	NSYSCALLS
};

#endif /* !JOS_INC_SYSCALL_H */
