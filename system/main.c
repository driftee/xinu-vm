/*  main.c  - main */

#include <xinu.h>



process	main(void)
{
	

	// char *b = allocmem(40900);
	// deallocmem(b, PLACEHOLDER);
	for(int i = 0; i < 10; i ++)
	{
		char *a = allocmem(4090);
		char *c = allocmem(8190);
		char *d = allocmem(40000);
		deallocmem(c, PLACEHOLDER);
		deallocmem(a, PLACEHOLDER);
		deallocmem(d, PLACEHOLDER);
	}
	recvclr();
	resume(create(shell, 8192, 50, "shell", 1, CONSOLE));

	// /* Wait for shell to exit and recreate it */

	while (TRUE) {
		receive();
		sleepms(200);
		kprintf("\n\nMain process recreating shell\n\n");
		resume(create(shell, 4096, 20, "shell", 1, CONSOLE));
	}
	return OK;

}
