from string import Template

NUM_OF_TRAPS = 52
def main():
	with open("kern/trapentry.tmpl") as f:
		trapentry_tmpl = Template(f.read())
	with open("kern/trap.c.tmpl") as f:
		trapc_tmpl = Template(f.read())
	
	entries = []
	for i in xrange(0,NUM_OF_TRAPS):
		if i==8 or (i>=10 and i<=14) or i==17:
			entry = '    TRAPHANDLER(trap_handler{0}, {0})'.format(i)
		else:
			entry = '    TRAPHANDLER_NOEC(trap_handler{0}, {0})'.format(i)
		entries.append(entry)
	with open("kern/trapentry.S", "w") as f:
		f.write(trapentry_tmpl.safe_substitute(trap_entries='\n'.join(entries)))

	trap_handlers = ['    extern void trap_handler%d();' % i 
				for i in xrange(0,NUM_OF_TRAPS)]
	set_gates = ['    SETGATE(idt[{0}], 0, GD_KT, trap_handler{0}, 0);'.format(i) 
				for i in xrange(0,NUM_OF_TRAPS)]
	# breakpoint can be generated in user mode
	set_gates[3] = '    SETGATE(idt[3], 0, GD_KT, trap_handler3, 3);'
	# syscall can be from user mode
	set_gates[48] = '    SETGATE(idt[48], 0, GD_KT, trap_handler48, 3);'
	with open("kern/trap.c", "w") as f:
		f.write(trapc_tmpl.safe_substitute(trap_handlers='\n'.join(trap_handlers), 
				gates='\n'.join(set_gates)))
if __name__ == '__main__':
	main()