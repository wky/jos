// program to cause a breakpoint trap

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	cprintf("Issue a breakpoint using 'int $3'...\n");
	asm volatile("int $3");
	cprintf("Recovered from breakpoint...\n");
}

