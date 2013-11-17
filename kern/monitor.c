// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/mmu.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/env.h>

#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
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
	{ "cpuinfo", "Display CPUID features", mon_cpuinfo },
	{ "showmappings", "Display virtual memory mappings in range", mon_showmappings},
	{ "memdump", "Display contents of virtual or physical memory", mon_memdump},
	{ "continue", "Continue execution after a debug/breakpoint exception", mon_continue},
	{ "singlestep", "Set Trap flag to enable single stepping for the current process", mon_singlestep},
	{ "stopstep", "Clear Trap flag to enable normal execution for the current process", mon_stopstep},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

static int
parse_hex(char* str, uintptr_t* addr){
	uintptr_t val = 0; 
	if (str[0] != '0' || str[1] != 'x')
		return -1;
	str = str+2;
	while (*str){
		val <<= 4;
		if (*str >= '0' && *str <= '9')
			val += *str - '0';
		else if (*str >= 'a' || *str <= 'f')
			val += *str - 'a' + 10;
		else if (*str >= 'A' || *str <= 'F')
			val += *str - 'A' + 10;
		else
			return -1;
		str++;
	}
	*addr = val;
	return 0;
}

static char*
conv_perm(uintptr_t pte){
	static char str[16];
	const static char flags[16] = "GSDACTUWP";
	int i;
	// 9 flag bits
	pte &= (1<<9) - 1;
	for (i = 8; i >= 0; --i, pte>>=1)
		str[i] = (pte & 1)? flags[i] : '_';
	return str;
}
#ifndef MEMDUMP_SIZE 
#define MEMDUMP_SIZE 16
#endif

#ifndef PG_BEGIN
#define PG_BEGIN(va) ((va) & ~(1<<PGSHIFT))
#endif

int
mon_continue(int argc, char **argv, struct Trapframe *tf){
	if (tf == NULL){
		cprintf("No trapframe to continue!\n");
		return 0;
	}
	cprintf("Continue execution of user program ...\n");
	env_pop_tf(tf); // noreturn
	return 0;
}
int
mon_singlestep(int argc, char **argv, struct Trapframe *tf){
	if (tf == NULL){
		cprintf("No trapframe!\n");
		return 0;
	}
	tf->tf_eflags |= FL_TF;
	cprintf("Trap flag set.\n");
	return 0;
}
int
mon_stopstep(int argc, char **argv, struct Trapframe *tf){
	if (tf == NULL){
		cprintf("No trapframe!\n");
		return 0;
	}
	tf->tf_eflags &= ~FL_TF;
	cprintf("Trap flag cleared.\n");
	return 0;
}

int
mon_memdump(int argc, char **argv, struct Trapframe *tf){
	// memdump p/v 0x0000 (0x000) 
	uintptr_t begin = 0, len = 1, va;
	int pgbegin, i, j; 
	if (argc < 3
		|| !(argv[1][0] == 'p' || argv[1][0] == 'v')
		|| parse_hex(argv[2], &begin))
	{
	show_memdump_usage:
		cprintf("Usage: %s physical/virtual start_addr [length]\n", argv[0]);
		cprintf("Sample: %s v 0xf0000000 0x80\n", argv[0]);
		return 0;
	}
	if (argc > 3 && parse_hex(argv[3], &len))
		goto show_memdump_usage;
	begin = ROUNDDOWN(begin, MEMDUMP_SIZE);
	len = ROUNDUP(len, MEMDUMP_SIZE);
	if (argv[1][0] == 'p'){
		for (i = 0; i < len; i += MEMDUMP_SIZE){
			if (PGNUM(begin + i) >= npages){
				cprintf("Exceeds physical memory limit.\n");
				break;
			}
			va = begin + KERNBASE + i;
			cprintf("\x1b[10m%08p:", begin + i);
			for (j = 0; j < MEMDUMP_SIZE; j += 4){
				cprintf(" \x1b[9m%08x", *((int*)(va + j)));
			}
			cprintf("\n\x1b[0m");
		}
	}else{// virtual memory
		// trick
		pgbegin = PG_BEGIN(begin) + PGSIZE;
		for (i = 0; i < len; i += MEMDUMP_SIZE){
			// at page boundary, must check availibility
			if (PG_BEGIN(begin) != pgbegin){
				pgbegin = PG_BEGIN(begin + i);
				if (page_lookup(kern_pgdir, (void*)pgbegin, NULL) == NULL){
					cprintf("Page not mapped: \x1b[4m%08p\x1b[0m\n", pgbegin);
					break;
				}
			}
			cprintf("\x1b[10m%08p:", begin + i);
			for (j = 0; j < MEMDUMP_SIZE; j += 4){
				cprintf(" \x1b[9m%08x", *((int*)(begin + i + j)));
			}
			cprintf("\n\x1b[0m");
		}
	}
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf){
	uintptr_t begin, end;
	struct PageInfo *pp;
	pte_t *pte_ptr;
	if (argc < 3 
		|| parse_hex(argv[1], &begin) 
		|| parse_hex(argv[2], &end))
	{
		cprintf("Usage: %s start_addr end_addr\nSample: %s 0x3000 0x5000\n", 
			argv[0], argv[0]);
		return 0;
	}
	begin = ROUNDUP(begin, PGSIZE);
	end = ROUNDUP(end, PGSIZE);
	if (end < begin){
		cprintf("The second address must be greater than the first.\n");
		return 0;
	}
	cprintf("Virt Addr   Permissions  Phys Addr\n");
	for (; begin <= end; begin += PGSIZE){
		pp = page_lookup(kern_pgdir, (void*)begin, &pte_ptr);
		if (pp && (*pte_ptr & PTE_P))
			cprintf("\x1b[2m%08p\x1b[0m   \x1b[12m%s\x1b[0m   \x1b[3m%08p\x1b[0m\n", begin, conv_perm(*pte_ptr), page2pa(pp));
		else
			cprintf("\x1b[2m%08p\x1b[0m   \x1b[4mINVALID\x1b[0m\n", begin);
	}
	return 0;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("\x1b[14m%s\x1b[0m - %s\n", commands[i].name, commands[i].desc);
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
	cprintf("  _start                  \x1b[4m%08x\x1b[0m (phys)\n", _start);
	cprintf("  entry  \x1b[4m%08x\x1b[0m (virt)  \x1b[4m%08x\x1b[0m (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  \x1b[4m%08x\x1b[0m (virt)  \x1b[4m%08x\x1b[0m (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  \x1b[4m%08x\x1b[0m (virt)  \x1b[4m%08x\x1b[0m (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    \x1b[4m%08x\x1b[0m (virt)  \x1b[4m%08x\x1b[0m (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: \x1b[4m%d\x1b[0mKB\n",
		ROUNDUP(end - entry, 1024) / 1024);

	// wky_test_function();

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
			cprintf("\x1b[3m%s\x1b[0m ", cpuid_feature_string(i));
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
		cprintf("  ebp \x1b[9m%08x\x1b[0m  eip \x1b[9m%08x\x1b[0m  args \x1b[9m%08x %08x %08x %08x %08x\x1b[0m\n",
			bp, bp[1], bp[2], bp[3], bp[4], bp[5], bp[6]);
		// load and display debug info
		debuginfo_eip(bp[1], &info);
		cprintf("         \x1b[14m%s\x1b[0m:\x1b[2;28m%d\x1b[0;20m: \x1b[4m%.*s+\x1b[28m%d\x1b[0;20m\n", 
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
