/* initialize.c - nulluser, sysinit */

/* Handle system initialization and become the null process */

#include <xinu.h>
#include <string.h>

extern	void	start(void);	/* Start of Xinu code			*/
extern	void	*_end;		/* End of Xinu code			*/

/* Function prototypes */

extern	void main(void);	/* Main is the first process created	*/
static	void sysinit(); 	/* Internal system initialization	*/
extern	void ScanFreePages(void);	/* Initializes the free memory list	*/
local	process startup(void);	/* Process to finish startup tasks	*/

/* Declarations of major kernel variables */

struct	procent	proctab[NPROC];	/* Process table			*/
struct	sentry	semtab[NSEM];	/* Semaphore table			*/
struct	memblk	memlist;	/* List of free memory blocks		*/

/* Active system status */

int	prcount;		/* Total number of live processes	*/
pid32	currpid;		/* ID of currently executing process	*/
uint32 is_page;

/* Control sequence to reset the console colors and cusor positiion	*/

#define	CONSOLE_RESET	" \033[0m\033[2J\033[;H"



extern struct page *GetOnePage(void);
extern void free_page(struct page *addr);
extern void test(void);

extern char *allocstk(
	uint32 nbytes, /* Size of memory requested	*/
	uint32 pgdir);



static inline void lcr3(uint32 val)
{
	asm volatile(
		"mov %0, %%cr3\n\t"
		:
		: "r"(val)
		:);
}

void InitializeVirtualMemory(void)
{
	struct page *static_pages = (struct page *)(KERNEL_END - PAGE_SIZE * 10);
	memset(static_pages, 0, PAGE_SIZE);
	int i, j;
	for (i = 0; i <= 7; i++)
	{
		static_pages[0].entries[i] = (uint32)((char *)static_pages + ((i + 1) * PAGE_SIZE)) | PTE_P | PTE_W;
		for (j = 0; j < 1024; j++)
		{
			static_pages[i + 1].entries[j] = ((i << 10) + j) << 12 | PTE_P | PTE_W;
		}
	}
	static_pages[0].entries[1021] = (uint32)((char *)static_pages + (9 * PAGE_SIZE)) | PTE_P | PTE_W;
	memset((char *)static_pages + 9 * PAGE_SIZE, 0, PAGE_SIZE);
	static_pages[9].entries[1023] = 0x6000 | PTE_P | PTE_W;
	lcr3((uint32)static_pages);

	is_page = 1;
	asm volatile(
		"orl $0xff7ff000, %%esp\n\t"
		"orl $0xff7ff000, %%ebp\n\t"
		"movl %%cr0, %%eax\n\t"
		"orl $0x80000000, %%eax\n\t"
		"movl %%eax, %%cr0\n\t"
		:
		:
		: "eax");
}

/* nulluser - vm version */
void nulluser()
{
	is_page = 0;
	// kprintf("[I](nulluser) nulluser launched. Recording free pages...\n");
	ScanFreePages(); // record free pages
	// kprintf("[I](nulluser) free pages recorded. Initializing vm...\n");
	InitializeVirtualMemory(); // initialize virtual memory
	// kprintf("[I](nulluser) vm intialized. Initializing system\n");
	sysinit();
	// kprintf("[I](nulluser) system intialized.\n");

	enable();

	/* Create a process to finish startup and start main */
	resume(create((void *)startup, INITSTK, INITPRIO,
				  "Startup process", 0, NULL));
	// kprintf("[I](nulluser) startup process created.\n");
	/* Become the Null process (i.e., guarantee that the CPU has	*/
	/*  something to run when no other process is ready to execute)	*/

	while (TRUE)
	{

		/* Halt until there is an external interrupt */

		asm volatile("hlt");
	}
}



/*------------------------------------------------------------------------
 *
 * startup  -  Finish startup takss that cannot be run from the Null
 *		  process and then create and resumethe main process
 *
 *------------------------------------------------------------------------
 */
local process	startup(void)
{
	/* Create a process to execute function main() */

	resume(create((void *)main, INITSTK, INITPRIO,
					"Main process", 0, NULL));

	/* Startup process exits at this point */

	return OK;
}


/*------------------------------------------------------------------------
 *
 * sysinit  -  Initialize all Xinu data structures and devices
 *
 *------------------------------------------------------------------------
 */
static	void	sysinit()
{
	int32	i;
	struct	procent	*prptr;		/* Ptr to process table entry	*/
	struct	sentry	*semptr;	/* Ptr to semaphore table entry	*/

	/* Platform Specific Initialization */

	platinit();

	/* Reset the console */

	kprintf(CONSOLE_RESET);
	kprintf("\n%s\n\n", VERSION);

	/* Initialize the interrupt vectors */

	initevec();

	/* Initialize system variables */

	/* Count the Null process as the first process in the system */

	prcount = 1;

	/* Scheduling is not currently blocked */

	Defer.ndefers = 0;

	/* Initialize process table entries free */

	for (i = 0; i < NPROC; i++) {
		prptr = &proctab[i];
		prptr->prstate = PR_FREE;
		prptr->prname[0] = NULLCH;
		prptr->prstkbase = NULL;
		prptr->prprio = 0;
		prptr->phypgdir = NULL;
	}

	/* Initialize the Null process entry */	

	prptr = &proctab[NULLPROC];
	prptr->prstate = PR_CURR;
	currpid = NULLPROC;
	prptr->prprio = 0;
	strncpy(prptr->prname, "prnull", 7);
	prptr->phypgdir = KERNEL_END - PAGE_SIZE * 10;
	prptr->prstkbase = allocstk(NULLSTK, prptr->phypgdir);
	prptr->prstklen = NULLSTK;
	prptr->prstkptr = (char *)prptr->prstkbase - prptr->prstklen;
	prptr->freelistptr = NULL;
	prptr->maxheap = KERNEL_END;
	
	/* Initialize semaphores */

	for (i = 0; i < NSEM; i++) {
		semptr = &semtab[i];
		semptr->sstate = S_FREE;
		semptr->scount = 0;
		semptr->squeue = newqueue();
	}

	/* Initialize buffer pools */

	bufinit();

	/* Create a ready list for processes */

	readylist = newqueue();

	/* Initialize the real time clock */

	clkinit();

	for (i = 0; i < NDEVS; i++) {
		init(i);
	}
	return;
}

int32	stop(char *s)
{
	kprintf("%s\n", s);
	kprintf("looping... press reset\n");
	while(1)
		/* Empty */;
}

int32	delay(int n)
{
	DELAY(n);
	return OK;
}
