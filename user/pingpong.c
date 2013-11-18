// Ping-pong a counter between two processes.
// Only need to start one of these -- splits into two with fork.

#include <inc/lib.h>

#define MAXNUM 100
void
umain(int argc, char **argv)
{
	envid_t who;

	if ((who = fork()) != 0) {
		// get the ball rolling
		cprintf("send 0 from %x to %x\n", sys_getenvid(), who);
		ipc_send(who, 0, 0, 0);
	}

	while (1) {
		uint32_t i = ipc_recv(&who, 0, 0);
		cprintf("%x got %d from %x\n", sys_getenvid(), i, who);
		if (i == MAXNUM)
			return;
		i++;
		ipc_send(who, i, 0, 0);
		if (i == MAXNUM)
			return;
	}

}

