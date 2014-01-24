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
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

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
	{ "backtrace", "Monitor the back trace of a function", mon_backtrace},
	{ "showmappings", "Display all the physics page mappings that apply to particular range of virtual addr", mon_showmappings},
	{ "setmappings", "Explicitly set, clear or change the permissions of any mapping in the current address space", mon_setmappings},
	{ "dumpmem", "Dump the contents of a range of memory given either a virtual of physical address", mon_dumpmem},
	{ "continue", "Continue execution for debugging", mon_continue},
	{ "si", "Single step for debugging", mon_si},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

int
mon_dumpmem(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 4) {
		cprintf("Usage: dumpmem [ADDR_TYPE] [LOWER_ADDR] [DWORD]\n");
		cprintf("ADDR_TYPE should be 'v' or 'p'. v: virtual address, p: physical address.\n"); 
		return 0;
	}
	uint32_t la,ua;
	int i;
	if (argv[1][0] == 'p') {
		la = strtol(argv[2],0,0) + KERNBASE;
		ua = la + strtol(argv[3],0,0)*4;
	} else if (argv[1][0] == 'v') {
		la = strtol(argv[2],0,0);
		ua = la + strtol(argv[3],0,0)*4;
	} else {
		cprintf("Invalid ADDR TYPE!\n");
		return 0;
	}
	if (la >= ua ||
	    la != ROUNDUP(la, 4) ||
	    ua != ROUNDUP(ua, 4)) {
		cprintf("Invalid ADDR (0x%x, 0x%x)!\n",la,ua);
		return 0;
	}
	for(i=0 ;la < ua; la += 4) {
		if(!(i%4))
			cprintf("\n0x%x: ",la);
		cprintf("0x%x\t",*((uint32_t *)(la)));
		i++;
	}
	cprintf("\n");
	return 0;
}
int
mon_setmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("Usage: setmappings [VIRTURAL_ADDR] [PERMISSION]\n");
		cprintf("address should be aligned in 4kb, permission should \n \
			 be a combination of u/k and r/w. \n \
			 u:user, k:kernel, r:readonly, w:read/write.\n");
		return 0;
	}
	uint32_t va = strtol(argv[1],0,0);
	char* perm = argv[2];
	if (va != ROUNDUP(va, PGSIZE)) {
		cprintf("Invalid address: 0x%x!\n",va);
		return 0;
	}
	if (((perm[0] != 'k') && (perm[0] != 'u')) ||
		((perm[1] != 'r') && (perm[1] != 'w'))) {
		cprintf("Invalid permission!\n");
		return 0;
	}
	pte_t *pte = pgdir_walk(kern_pgdir, (void *)va, 0);
	if (!pte) {
		cprintf("Address not mapped!\n");
		return 0;
	}
	if (perm[0] == 'u')
		*pte = (*pte) | PTE_U;
	else
		*pte = (*pte) & (~PTE_U);
	if (perm[1] == 'w')
		*pte = (*pte) | PTE_W;
	else
		*pte = (*pte) & (~PTE_W);
	cprintf("set 0x%x permission as 0x%x!\n",va, PGOFF(*pte));
	return 0;
}
	
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t start, end;
	pte_t *pte;
	if (argc != 3) {
		cprintf("Usage: showmappings [LOWER_ADDR] [UPPER_ADDR]\n");
		cprintf("Both address should be aligned in 4kb\n");
		return 0;
	}
	start = strtol(argv[1],0,0);
	end = strtol(argv[2],0,0);
	
	if (start != ROUNDUP(start, PGSIZE) ||
	    end != ROUNDUP(end, PGSIZE) ||
	    start > end) {
		cprintf("Invalid address\n");
		return 0;
	}
	while (start < end) {
		pte = pgdir_walk(kern_pgdir, (void *)start, 0);
		cprintf("0x%x ~ 0x%x: ",start, start + PGSIZE);
		if (pte == NULL) {
			cprintf("Not mapped!\n");	
		} else {
			cprintf("0x%x\n",PTE_ADDR(*pte));
			if (*pte & PTE_U)
				cprintf("user:");
			else
				cprintf("kernel:");
			if (*pte & PTE_W)
				cprintf("read/write\n");
			else
				cprintf("read only\n");
		}
		start += PGSIZE;
	}
	return 0;
}

int
mon_si(int argc, char **argv, struct Trapframe *tf)
{
	extern struct Env *curenv;
	struct Eipdebuginfo info;
	if (tf == NULL) {
		cprintf("Can't single step, since tf == NULL\n");
		return -1;
	}
	if ((tf->tf_trapno != T_DEBUG) && (tf->tf_trapno != T_BRKPT)) {
		cprintf("Can't continue, wrong trap number!\n");
		return -1;
	}
	cprintf("Print the single step info!\n");
	tf->tf_eflags |= FL_TF;
	debuginfo_eip((uintptr_t)tf->tf_eip, &info);
	cprintf("Si information: \n tf_eip = %x,\n%s:%d: %.*s+%d\n",tf->tf_eip, info.eip_file,info.eip_line,info.eip_fn_namelen, info.eip_fn_name,tf->tf_eip - info.eip_fn_addr);
	env_run(curenv);
	return 0;
}
	
int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	extern struct Env *curenv;
	if (tf == NULL) {
		cprintf("Can't continue, since the tf = NULL!\n");
		return -1;
	}
	if ((tf->tf_trapno != T_DEBUG) && (tf->tf_trapno != T_BRKPT)) {
		cprintf("Can't continue, wrong trap number!\n");
		return -1;
	}
	tf->tf_eflags &= ~FL_TF;
	env_run(curenv);
	return 0;
}
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Jacky 20140101: Implement the backtrace monitor function
	int i;
	uint32_t ebp = read_ebp();
	uint32_t eip;
	char fn_name[256];
	struct Eipdebuginfo info;
	cprintf("Stack backtrace:\n");
	// The key of args is print the every single byte of (ebp) - ebp
	while(ebp != 0) {
		eip = *(int*)(ebp+4);
		cprintf("ebp %x eip %x args ", ebp, eip);
		for (i = 1; i < 20; i++) {
			if (*(int*)ebp != ((ebp + i*4))) {
				cprintf("%08x ", *(int*)((ebp+4)+i*4));
			} else 
				break;
		}
		cprintf("\n");
		debuginfo_eip((uintptr_t)eip, &info);
		memmove(fn_name,info.eip_fn_name,info.eip_fn_namelen);
		fn_name[info.eip_fn_namelen] = '\0';
		cprintf("%s:%d: %s+%x \n",info.eip_file, info.eip_line, 
				fn_name, eip - info.eip_fn_addr);
		ebp = (*(int*)(ebp));
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
//	cprintf("%CredWelcome %Cgrnto the JOS kernel monitor!\n");
//	cprintf("Type 'help' %C142for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
