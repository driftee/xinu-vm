/* create.c - create, newpid */

#include <xinu.h>

local int newpid();

extern struct page *GetOnePage(void);
extern char *allocstk(
	uint32 nbytes, /* Size of memory requested	*/
	uint32 pgdir);
/*------------------------------------------------------------------------
 *  create  -  Create a process to start running a function on x86
 *------------------------------------------------------------------------
 */
pid32 create(
	void *funcaddr, /* Address of the function	*/
	uint32 ssize,	/* Stack size in bytes		*/
	pri16 priority, /* Process priority > 0		*/
	char *name,		/* Name (for debugging)		*/
	uint32 nargs,	/* Number of args that follow	*/
	...)
{
	uint32 savsp, *pushsp;
	intmask mask;		   /* Interrupt mask		*/
	pid32 pid;			   /* Stores new process id	*/
	struct procent *prptr; /* Pointer to proc. table entry */
	int32 i;
	uint32 *a;	   /* Points to list of args	*/
	uint32 *saddr; /* Stack address		*/

	mask = disable();
	if (ssize < MINSTK)
		ssize = MINSTK;
	ssize = (uint32)roundmb(ssize);
	// if ( (priority < 1) || ((pid=newpid()) == SYSERR) ||
	//      ((saddr = (uint32 *)getstk(ssize)) == (uint32 *)SYSERR) ) {
	// 	restore(mask);
	// 	return SYSERR;
	// }
	if ((pid = newpid()) == SYSERR)
	{
		restore(mask);
		return SYSERR;
	}

	prcount++;
	prptr = &proctab[pid];
	prptr->phypgdir = (uint32)GetOnePage();

	if ((priority < 1) ||
		((saddr = (uint32 *)allocstk(ssize, prptr->phypgdir)) == (uint32 *)SYSERR))
	{
		restore(mask);
		return SYSERR;
	}

	/* Initialize process table entry for new process */
	prptr->prstate = PR_SUSP; /* Initial state is suspended	*/
	prptr->prprio = priority;
	prptr->prstkbase = (char *)saddr;
	prptr->prstklen = ssize;
	prptr->prname[PNMLEN - 1] = NULLCH;
	for (i = 0; i < PNMLEN - 1 && (prptr->prname[i] = name[i]) != NULLCH; i++)
		;
	prptr->prsem = -1;
	prptr->prparent = (pid32)getpid();
	prptr->prhasmsg = FALSE;

	/* Set up stdin, stdout, and stderr descriptors for the shell	*/
	prptr->prdesc[0] = CONSOLE;
	prptr->prdesc[1] = CONSOLE;
	prptr->prdesc[2] = CONSOLE;

	prptr->freelistptr = NULL;
	prptr->maxheap = KERNEL_END;

	/* Initialize stack as if the process was called		*/

	uint32 *saddr_cp = saddr;
	saddr = (uint32 *)0x1fffffc;
	*saddr = STACKMAGIC;
	savsp = (uint32)saddr_cp;

	uint32 obj;
	/* Push arguments */
	a = (uint32 *)(&nargs + 1); /* Start of args		*/
	a += nargs - 1;				/* Last argument		*/
	for (; nargs > 0; nargs--)	/* Machine dependent; copy args	*/
	{
		obj = *a--;
		*--saddr = obj;
		--saddr_cp;
	}						  /* onto created process's stack	*/
	*--saddr = (long)INITRET; /* Push on return address	*/
	--saddr_cp;
	/* The following entries on the stack must match what ctxsw	*/
	/*   expects a saved process state to contain: ret address,	*/
	/*   ebp, interrupt mask, flags, registers, and an old SP	*/

	*--saddr = (long)funcaddr; /* Make the stack look like it's*/
	/*   half-way through a call to	*/
	/*   ctxsw that "returns" to the*/
	/*   new process		*/
	--saddr_cp;
	*--saddr = (uint32)prptr->prstkbase; /* This will be register ebp	*/
	/*   for process exit		*/
	--saddr_cp;
	savsp = (uint32)saddr_cp; /* Start of frame for ctxsw	*/
	*--saddr = 0x00000200;	  /* New process runs with	*/
	/*   interrupts enabled		*/
	--saddr_cp;
	/* Basically, the following emulates an x86 "pushal" instruction*/

	*--saddr = 0; /* %eax */
	--saddr_cp;
	*--saddr = 0; /* %ecx */
	--saddr_cp;
	*--saddr = 0; /* %edx */
	--saddr_cp;
	*--saddr = 0; /* %ebx */
	--saddr_cp;
	*--saddr = 0; /* %esp; value filled in below	*/
	--saddr_cp;
	pushsp = saddr;	  /* Remember this location	*/
	*--saddr = savsp; /* %ebp (while finishing ctxsw)	*/
	--saddr_cp;
	*--saddr = 0; /* %esi */
	--saddr_cp;
	*--saddr = 0; /* %edi */
	--saddr_cp;
	*pushsp = (unsigned long)(prptr->prstkptr = (char *)saddr_cp);
	restore(mask);
#ifdef DEBUG_INFO
	kprintf("[I](create) new process (pid: %d, prname: %s) finished.\n", pid, name);
#endif

	return pid;
}

/*------------------------------------------------------------------------
 *  newpid  -  Obtain a new (free) process ID
 *------------------------------------------------------------------------
 */
local pid32 newpid(void)
{
	uint32 i;				  /* Iterate through all processes*/
	static pid32 nextpid = 1; /* Position in table to try or	*/
	/*   one beyond end of table	*/

	/* Check all NPROC slots */

	for (i = 0; i < NPROC; i++)
	{
		nextpid %= NPROC; /* Wrap around to beginning */
		if (proctab[nextpid].prstate == PR_FREE)
		{
			return nextpid++;
		}
		else
		{
			nextpid++;
		}
	}
	kprintf("newpid error\n");
	return (pid32)SYSERR;
}
