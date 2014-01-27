// Called from entry.S to get us going.
// entry.S already took care of defining envs, pages, uvpd, and uvpt.

#include <inc/lib.h>

extern void umain(int argc, char **argv);

const volatile struct Env *thisenv;
const char *binaryname = "<unknown>";

void
libmain(int argc, char **argv)
{
	// set thisenv to point at our Env structure in envs[].
	// LAB 3: Jacky 140125
	envid_t	id = sys_getenvid();
	if (id < 0) {
		cprintf("%s()incorrect envid!\n",__func__);
		exit();
	}
//	thisenv = (struct Env *)(&envs + sizeof(struct Env)*id);
	thisenv = envs + ENVX(id);

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}

