// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <lib/cpuid.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display detailed kernel stack trace", mon_backtrace },
	{ "cpuinfo", "Display CPUID features", mon_cpuinfo }
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

void
wky_test_function(void){
	int x = 1, y = 3, z = 4;
	cprintf("x %d, y %x, z %d\n", x, y, z);
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);

	wky_test_function();

	return 0;
}

int
mon_cpuinfo(int argc, char **argv, struct Trapframe *tf){
	int i;
	char buffer[CPUID_BRAND_LENGTH];
	cpuid_brand(buffer, sizeof(buffer));
	cprintf("%s\n", buffer);
	for (i = 0; i < CPUID_FEATURE_LENGTH; i++) {
		if (cpuid_feature(i)) {
			cprintf("%s ", cpuid_feature_string(i));
		}
	}
	cprintf("\n");
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// base pointer
	unsigned int *bp;
	struct Eipdebuginfo info;
	cprintf("Stack backtrace:\n");
	// load %ebp to bp
	__asm__ ("movl %%ebp, %0":"=r"(bp) :);
	// entry.S set the first %ebp to be zero
	while (bp != 0){
		// display ebp, return address, and parameters on stack
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
			bp, bp[1], bp[2], bp[3], bp[4], bp[5], bp[6]);
		// load and display debug info
		debuginfo_eip(bp[1], &info);
		cprintf("         %s:%d: %.*s+%d\n", 
			info.eip_file, info.eip_line, 
			info.eip_fn_namelen, info.eip_fn_name, 
			bp[1]-(unsigned int)info.eip_fn_addr);
		// bp points to the previous stack frame's saved %ebp
		bp = (unsigned int*)(bp[0]);
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
