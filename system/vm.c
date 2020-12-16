/* vm.c - what makes virtual memory work */

#include <xinu.h>

uint32 is_page;

extern uint32 free_pages;
extern uint32 *ptpt;
extern uint32 total_pages;
extern uint32 phypagemax;



void FlushTlb(void *page)
{
    asm volatile(
        "invlpg (%0)\n\t"
        :
        : "r"(page)
        : "memory");
}
inline void MkpgAccessibleby0x1fff000(uint32 paddr)
{
    intmask mask = disable();
    ((struct page *)(KERNEL_END - 2 * PAGE_SIZE))->entries[1023] = paddr | PTE_P | PTE_W; // now we hope that pddr's physical page can be accessed by 0x1fff000
    FlushTlb((struct page *)0x1fff000);
    restore(mask);
}


uint32 Bytes2Pages(uint32 nbytes)
{
    uint32 result = nbytes / PAGE_SIZE;
    if ((nbytes % PAGE_SIZE))
    {
        result++;
    }
    return result;
}

uint32 Pages2Tbls(uint32 npages)
{
    uint32 result = npages / 1024;
    if ((npages % 1024))
    {
        result++;
    }
    return result;
}

struct page *GetOnePage(void)
{
    intmask mask = disable();
    int i;
    uint32 phy_addr;
    for (i = total_pages; i > 0; i--)
    {
        phy_addr = *(ptpt - i);
        if ((phy_addr & 1) == 0)
        {
            *(ptpt - i) = phy_addr | 1;
#ifdef DEBUG_INFO
            kprintf("[I](GetOnePage) get page's physical address: 0x%x\n", (uint32)phy_addr);
#endif
            if (is_page)
            {
                MkpgAccessibleby0x1fff000(phy_addr);
                memset((void *)0x1fff000, 0, PAGE_SIZE);
            }
            else
            {
                memset((void *)phy_addr, 0, PAGE_SIZE);
            }
            restore(mask);
            return (void *)phy_addr;
        }
    }
    restore(mask);
    panic("Out of memory");
    return NULL;
}

void FreeOnePage(struct page *addr)
{
    if ((uint32)addr < 1024 * 1024 * 32)
    {
        kprintf("[E](FreeOnePage) attempts to free a kernel page %x\n", addr);
        return;
    }

    if ((uint32)addr & 0xfff)
    {
        kprintf("[W](FreeOnePage) page address try to free: last 12 bits is not zero\n");
        addr = (struct page *)((uint32)addr & ~0xfff);
    }
    uint32 index = (phypagemax - (uint32)addr) / PAGE_SIZE;
    uint32 *PhysicalPagesPoolBase = ptpt - total_pages;
    PhysicalPagesPoolBase[index] &= ~1;
#ifdef DEBUG_INFO
    kprintf("[I](FreeOnePage) free 0x%x succeed\n", addr);
#endif
}

/*
 * Set boundary tag for allocated blocks
 */
void set_alloc_boundary_tag(uint32 *ptr, uint32 size)
{
    *(ptr - 1) = size | 1;
    *(ptr + size / 4 - 2) = size | 1;
}

/*
 * Set boundary tag for free blocks
 */
void set_free_boundary_tag(uint32 *ptr, uint32 size)
{
    *(ptr - 1) = size;
    *(ptr + size / 4 - 2) = size;
}

/*
 * Insert new free block into the free list
 */
void insert_free_block(uint32 *ptr)
{
    uint32 *first = (uint32 *)proctab[currpid].freelistptr;
    if (!first)
    { // empty free list
        *first = (uint32)ptr;
        proctab[currpid].freelistptr = (uint32)ptr;
        *ptr = (uint32)ptr;
        *(ptr + 1) = (uint32)ptr;
        return;
    }
    uint32 *next = (uint32 *)*first;

    uint32 *block_before = (uint32 *)*(next + 1);
    *(next + 1) = (uint32)ptr;
    *block_before = (uint32)ptr;
    *ptr = (uint32)next;
    *(ptr + 1) = (uint32)block_before;
}

/*
 * Delete a freeblock from the free list.
 */
void delete_free_block(uint32 *ptr)
{
    uint32 *after = (uint32 *)*ptr;
    uint32 *first = (uint32 *)proctab[currpid].freelistptr;
    if (after == ptr)
    { // The free list contains a free block only
        *first = 0;
        proctab[currpid].freelistptr = NULL;
        return;
    }
    uint32 *before = (uint32 *)*(ptr + 1);
    *(after + 1) = (uint32)before;
    *before = (uint32)after;
    if (*first == (uint32)ptr)
    { // If the guard points to the block to be deleted, then change the guard.
        *first = (uint32)after;
        proctab[currpid].freelistptr = *first;
    }
}

/*
 * Assign the old free block to the new and set the size
 */
void assign_free_block(uint32 *old, uint32 *new, uint32 newsize)
{
    uint32 *first = (uint32 *)proctab[currpid].freelistptr;
    set_free_boundary_tag(new, newsize);
    FlushTlb((void *)old);
    if (*old == (uint32)old)
    { // The freelist contains one free block only
        *first = (uint32) new;
        proctab[currpid].freelistptr = *first;
        *(new + 1) = (uint32) new;
        *new = (uint32) new;
        return;
    }
    uint32 *prev = (uint32 *)*(old + 1);
    uint32 *next = (uint32 *)*old;
    *prev = (uint32) new;
    *(next + 1) = (uint32) new;
    *new = (uint32)next;
    *(new + 1) = (uint32)prev;

}

/*
 * Scan in the free list to find the free block BEST for newsize.
 * Return a pointer to a block with allocated boundary tag set.(Return 0 if no block is found)
 * The free list changes accordingly.
 */
uint32 *scan_free_block(uint32 newsize)
{
    uint32 *first = (uint32 *)proctab[currpid].freelistptr;
    uint32 *current = (uint32 *)*first;
    if (!current)
    { // empty free list, MAYBE this won't happen, before scan, we make sure first points to a free block
        return 0;
    }
    uint32 min_quan = ~0;
    uint32 *bestfit = NULL;
    int fit_flag = 0;
    while (1)
    {
        uint32 free_size = *(current - 1);
        if (newsize <= free_size - 16)
        {
            if (min_quan > free_size - newsize)
            {
                fit_flag = 1;
                min_quan = free_size - newsize;
                bestfit = current;
            }
        }
        if (newsize <= free_size && newsize > free_size - 16)
        {
            set_alloc_boundary_tag(current, free_size);
            delete_free_block(current);
            return current;
        }

        current = (uint32 *)*current; // go to next free block
        if (*first == (uint32)current)
        {
            break;
            // return 0;
        }
    }
    if (!fit_flag)
    {
        return NULL;
    }

    uint32 free_size = *(bestfit - 1);
    uint32 left = free_size - newsize;
    set_alloc_boundary_tag(bestfit, newsize);
    assign_free_block(bestfit, bestfit + newsize / 4, left);
    return bestfit;
}

uint32 heapsbrk(uint32 nbytes) // please notice, this function will get some pages
{
#ifdef DEBUG_INFO
    kprintf("[I](heapsbrk) called by process(pid: %d, prname: %s), allocating %d bytes\n", currpid, proctab[currpid].prname, nbytes);
#endif
    uint32 pgdir = proctab[currpid].phypgdir;
    uint32 new_pages = Bytes2Pages(nbytes);
    uint32 ori_maxheap = proctab[currpid].maxheap;
    uint32 tbl_top = (ori_maxheap >> 12) & 0x3ff;
    uint32 dir_top = (ori_maxheap >> 22) & 0x3ff;
    uint32 tmp_page, tmp_tbl;
    uint32 new_maxheap = ori_maxheap + new_pages * PAGE_SIZE;
    while (new_pages > 0)
    {
        tmp_page = (uint32)GetOnePage();
        if (tbl_top == 0)
        { // page for a new table is needed;
            tmp_tbl = (uint32)GetOnePage();
            MkpgAccessibleby0x1fff000(pgdir);
            Write0x1fff000(dir_top, tmp_tbl | PTE_P | PTE_W);
            FlushTlb((void *)0x1fff000);
        }
        else
        {
            MkpgAccessibleby0x1fff000(pgdir);
            tmp_tbl = GetEntryFrom0x1fff000(dir_top) & ~0xfff;
        }
        if (tbl_top == 1023)
        {
            dir_top++;
        }
        MkpgAccessibleby0x1fff000(tmp_tbl);
        Write0x1fff000(tbl_top++, tmp_page | PTE_P | PTE_W);
        FlushTlb((void *)0x1fff000);
        tbl_top %= 1024;
        new_pages--;
    }
    proctab[currpid].maxheap = new_maxheap;
    if ((dir_top << 22 | tbl_top << 12) != new_maxheap)
    {
        kprintf("[W](heapsbrk) something went wrong, new dir_top is %d, new tbl_top is %d, so maxheap should be (dir_top << 22 | tbl_top << 12) = %d, not %d",
                dir_top, tbl_top, (dir_top << 22 | tbl_top << 12), new_maxheap);
    }
    return ori_maxheap;
}

void InitializeFreeList()
{ // initialize the free list
    // kprintf("1");
    uint32 *tmp_ptr = (uint32 *)heapsbrk(PAGE_SIZE);
    tmp_ptr += 1; // the actual pointer
    set_free_boundary_tag(tmp_ptr, PAGE_SIZE);
    *tmp_ptr = (uint32)tmp_ptr;       // next
    *(tmp_ptr + 1) = (uint32)tmp_ptr; // prev;
    proctab[currpid].freelistptr = (uint32)tmp_ptr;
    // kprintf("2");
}

void FreeHeapPage(uint32 pgaddr) // recycle the physical page according to the vm address pgaddr, and page table if necessary
{
    uint32 pgdir = proctab[currpid].phypgdir;
    uint32 dir_top = (pgaddr >> 22) & 0x3ff;
    uint32 tlb_top = (pgaddr >> 12) & 0x3ff;
    MkpgAccessibleby0x1fff000(pgdir);
    uint32 pgtb = GetEntryFrom0x1fff000(dir_top) & ~0x3ff; // physical address
    MkpgAccessibleby0x1fff000(pgtb);
    uint32 pg = GetEntryFrom0x1fff000(tlb_top) & ~0x3ff;
    FreeOnePage((struct page *)pg);
    if (tlb_top == 0)
    {
        FreeOnePage((struct page *)pgtb);
    }
}

char *allocmem(
    uint32 nbytes /* Size of memory requested	*/
)
{
    intmask mask = disable(); /* disable interrupt */
#ifdef DEBUG_INFO
    kprintf("[I](allocmem) starts to allocate %d bytes from heap\n", nbytes);
#endif
    if (proctab[currpid].freelistptr == NULL)
    { /* initialize the free list */
        InitializeFreeList();
    }

    uint32 newsize = ALIGN(nbytes + SIZE_T_SIZE); // actual size needed

    // Seek in the free list first
    uint32 *current = scan_free_block(newsize);
    if (current != 0)
    {
#ifdef DEBUG_INFO
        kprintf("[I](allocmem) Get pointer 0x%x from freelist.\n", (uint32)current);
#endif
        restore(mask);
        return (char *)current;
    }
    // Cannot find a fine block from the free list
    uint32 last_byte = proctab[currpid].maxheap - 4;
    uint32 *pointer = (uint32 *)last_byte;
    if (!(*pointer & 1) && ((uint32)pointer > KERNEL_END))
    { // The last block in the heap is empty but it's not large enough, then we can expand it to a extent so that it can contain what it have to
        uint32 free_size = *pointer & ~3;
        current = pointer - free_size / 4 + 2;
        // Optsbrk(current, newsize);
        uint32 need_size = newsize - free_size;
        uint32 pages_needed = Bytes2Pages(need_size);
        uint32 total_size = pages_needed * PAGE_SIZE;
        uint32 extra_size = total_size - need_size;
        heapsbrk(total_size);
        if (extra_size < 16)
        {
            delete_free_block(current);
            set_alloc_boundary_tag(current, free_size + total_size);
        }
        else
        {
            assign_free_block(current, (uint32 *)(proctab[currpid].maxheap - extra_size + 4), extra_size);
            set_alloc_boundary_tag(current, newsize);
        }
#ifdef DEBUG_INFO
        kprintf("[I](allocmem) Get pointer 0x%x by sbrk.\n", (uint32)current);
#endif
        restore(mask);
        // kprintf("5");
        return (char *)current;
    }

    // No other means but sbrk

    uint32 pages_needed = Bytes2Pages(newsize);
    uint32 total_size = pages_needed * PAGE_SIZE;
    uint32 extra_size = total_size - newsize;

    uint32 *p;
    p = (uint32 *)heapsbrk(total_size);
    if (extra_size < 16)
    { // likewise, no new free block comes up
        set_alloc_boundary_tag(p + 1, total_size);
    }
    else
    { // there is a new free block
        set_alloc_boundary_tag(p + 1, newsize);
        set_free_boundary_tag((uint32 *)(proctab[currpid].maxheap - extra_size + 4), extra_size);
        insert_free_block((uint32 *)(proctab[currpid].maxheap - extra_size + 4));
    }
    if (p == (void *)(-1))
    {
        restore(mask);
        return NULL;
    }
    // set_alloc_boundary_tag(p + 1, newsize);
    // printf("Get pointer %lx by totally sbrk.\n", (uint32)(p + 1));
#ifdef DEBUG_INFO
        kprintf("[I](allocmem) Get pointer 0x%x by sbrk.\n", (uint32)(p + 1));
#endif
    restore(mask);
    return (char *)(p + 1);

    // return NULL;
}

syscall deallocmem(
    char *blkaddr, /* Pointer to memory block	*/
    uint32 nbytes  /* Size of block in bytes, because we use free list, this argument has no use	*/
)

{
    intmask mask = disable();
#ifdef DEBUG_INFO
    kprintf("[I](deallocmem) called to free 0x%x\n", blkaddr);
#endif
    if ((uint32)blkaddr < KERNEL_END)
    {
        kprintf("[E](deallocmem) free kernel memory\n");
        return SYSERR;
    }
    // printf("Freeing pointer %lx\n", (uint32)ptr);

    uint32 *current = (uint32 *)blkaddr;
    uint32 free_size = *(current - 1) & ~3;
    // uint32 another_size = *(current - 2) & ~3;
    // if(another_size > free_size)
    // {
    //     my_pause(free_size);
    //     current -= 1;
    //     free_size = another_size;
    // }
    // printf("the size to be freed is %ld\n", free_size);

    int flag_before = 0;

    if (!(*(current - 2) & 1) && (current - 2) > (uint32 *)KERNEL_END)
    { // Coalescing with the free block before

        // printf("Combining with the free block before\n");
        uint32 prev_size = *(current - 2) & ~3;

        current -= prev_size / 4; // current is pointed to the combined block
        free_size += prev_size;

        // check_free_list();
        flag_before = 1;
    }
    if (!(*(current + free_size / 4 - 1) & 1) && (current + free_size / 4 - 1) <= (uint32 *)((uint32)proctab[currpid].maxheap - 15))
    { // Coalescing with the free block after
        uint32 next_size = *(current + free_size / 4 - 1) & ~3;

        // delete it from free list
        delete_free_block(current + free_size / 4);

        free_size += next_size;
    }

    if (!flag_before)
    { // If the freed block didn't coalesce with the block before, then add it to the free list
        insert_free_block(current);
    }
    uint32 pos = (uint32)current;
    if (pos - 4 + free_size == proctab[currpid].maxheap)
    {
        while (free_size >= PAGE_SIZE)
        {
            FreeHeapPage(proctab[currpid].maxheap -= PAGE_SIZE);
            free_size -= PAGE_SIZE;
        }
    }
    if (free_size == 0)
    {
        delete_free_block(current);
    }
    else
    {
        set_free_boundary_tag(current, free_size);
    }
#ifdef DEBUG_INFO
    kprintf("[I](deallocmem) done.\n", blkaddr);
#endif
    restore(mask);
    return 0;
}

char *allocstk(
    uint32 nbytes, /* Size of memory requested	*/
    uint32 pgdir   /* New process's page directory */
)
{
    intmask mask = disable();
    uint32 pages_needed = Bytes2Pages(nbytes);
#ifdef DEBUG_INFO
    kprintf("[I](allocstk) process(pid: %d, prname: %s) allocates %d page(s) for the process whose page directory's physical address is 0x%x\n", currpid, proctab[currpid].prname, pages_needed, pgdir);
#endif
    MkpgAccessibleby0x1fff000(pgdir);
    for (int i = 0; i <= 7; i++)
    {
        Write0x1fff000(i, (KERNEL_END - (9 - i) * PAGE_SIZE) | PTE_P | PTE_W); // copy 0 - 32 MB
    }
    uint32 stk_pgtb = (uint32)GetOnePage(); // one page table is enough for 4MB‘s stack
#ifdef DEBUG_INFO
    kprintf("[I](allocstk) new process's stack page table's physical address is 0x%x\n", stk_pgtb);
#endif
    MkpgAccessibleby0x1fff000(pgdir);
    Write0x1fff000(1023, stk_pgtb | PTE_P | PTE_W); // virtual high 10 bits: 1111111111 -> 1023
    MkpgAccessibleby0x1fff000(stk_pgtb);            // now the only stack page table page can be accessed by 0x1fff000
    memset((void *)0x1fff000, 0, PAGE_SIZE);
    uint32 stk_pg, stk_pg_one;
    for (int i = 0; i < pages_needed; i++)
    {
        stk_pg = (uint32)GetOnePage();
        if (i == 0)
        {
#ifdef DEBUG_INFO
            kprintf("[I](allocstk) new process's stack page(last)'s physical address is 0x%x\n", stk_pg);
#endif
            stk_pg_one = stk_pg;
        }
        MkpgAccessibleby0x1fff000(stk_pgtb);              // now the only stack page table page can be accessed by 0x1fff000
        Write0x1fff000(1023 - i, stk_pg | PTE_P | PTE_W); // virtual low 10 bits from 1111111111 to 0
    }
    MkpgAccessibleby0x1fff000(stk_pg_one); // still use temp, then we can initialize the stack by 0x1fff000 in the rest of create
    restore(mask);
    return (char *)0xfffffffc;             // 4GB - 4
}

void deallocstk(uint32 pgdir)
{
    intmask mask = disable();
#ifdef DEBUG_INFO
    kprintf("[I](deallocstk) free stack's page directory 0x%x\n", pgdir);
#endif
    FreeOnePage((struct page *)pgdir);
    struct page *pgptr = (struct page *)0x1fff000;
    MkpgAccessibleby0x1fff000(pgdir);
    uint32 sv_pgdr;
    for (int i = 1023; i >= 8; i--)
    {
        if ((pgptr->entries[i] & PTE_P))
        {
#ifdef DEBUG_INFO
            kprintf("[I](deallocstk) free stack's page table whose physical address is 0x%x(on pgdir[%d], PA = 0x%x)\n", (uint32)(pgptr->entries[i]), i, pgdir + i * 4);
#endif
            sv_pgdr = GetEntryFrom0x1fff000(i) & ~0xfff;
            FreeOnePage((struct page *)sv_pgdr); // free all page tables
            MkpgAccessibleby0x1fff000(sv_pgdr);
            for (int j = 1023; j >= 0; j--)
            {
                if ((pgptr->entries[j] & PTE_P) && (pgptr->entries[j] & PTE_W))
                {
#ifdef DEBUG_INFO
                    kprintf("[I](deallocstk) free stack's page 0x%x\n", (uint32)(pgptr->entries[j] & ~0xfff));
#endif
                    FreeOnePage((struct page *)(pgptr->entries[j] & ~0xfff)); // free all pages
                }
            }
            MkpgAccessibleby0x1fff000(pgdir); // make page dir accessible again to continue the loop
        }
    }
    restore(mask);
}

