#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;

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

	// LAB 4: Jacky 140128.
	int i, pos = 0;
	struct Env *thisenv = thiscpu->cpu_env;
#if 0
	if (thisenv == NULL)
		thisenv = &envs[0];
	cprintf("%s(),%d!\n",__func__,__LINE__);
	// find the position of thisenv in envs[]
	for (i = 0; i < NENV; i++) {
		if (&envs[i] == thisenv) {
			pos = i;
			break;
		}
	}
	cprintf("%s(),%d pos = %d!\n",__func__,__LINE__,pos);
	for (i = (pos+1)%NENV; i != pos; i = (i+1)%NENV) {
		if (envs[i].env_status == ENV_RUNNABLE) {
			// Following are all done in env_run()
			//c->cpu_env->env_status = ENV_RUNNABLE;
			//c->cpu_env = &envs[i];
			//unlock_kernel();
cprintf("%s(),%d, i = %d!\n",__func__,__LINE__,i);
			env_run(&envs[i]);
		}
	}
	//if ((i == pos) && (thisenv->env_status == ENV_RUNNING)) {
	if ((thisenv->env_status == ENV_RUNNING) || (thisenv->env_status == ENV_RUNNABLE)) {
			//unlock_kernel();
	cprintf("%s(),%d, run the orginal env, cpu = %d!\n",__func__,__LINE__,cpunum());
			env_run(thisenv);
	}
#else
	if (thisenv != NULL) {
		// find the position of thisenv in envs[]
		for (i = 0; i < NENV; i++) {
			if (&envs[i] == thisenv) {
				pos = i;
				break;
			}
		}
	}
	for (i = (pos+1)%NENV; i != pos; i = (i+1)%NENV) {
		if (envs[i].env_status == ENV_RUNNABLE) {
			env_run(&envs[i]);
		}
	}
	if (envs[i].env_status == ENV_RUNNABLE) { // envs[pos] will be exclusive by above loop
		env_run(&envs[i]);
	}
	if (thisenv != NULL) {
		if (thisenv->env_status == ENV_RUNNING) {
			env_run(thisenv);
		}
	}
#endif
	// sched_halt never returns
	sched_halt();
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
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING))
			break;
	}
	static int j = 0;
	j++;
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

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

