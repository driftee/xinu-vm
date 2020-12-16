typedef uint32 table_entry;

struct page
{
    table_entry entries[1024];
};

#define KERNEL_END 0x2000000
#define PAGE_SIZE 4096

#define PTE_P 0x001 // Present
#define PTE_W 0x002 // Writeable
#define PTE_U 0x004 // User

#define Write0x1fff000(index, entry) ((struct page *)0x1fff000)->entries[index] = entry
#define GetEntryFrom0x1fff000(index) ((struct page *)0x1fff000)->entries[index]
void MkpgAccessibleby0x1fff000(uint32 paddr);

extern uint32 is_page;

uint32 heapsbrk(uint32 nbytes);

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(uint32)))

char *allocmem(
    uint32 nbytes /* Size of memory requested	*/
);

syscall deallocmem(
    char *blkaddr, /* Pointer to memory block	*/
    uint32 nbytes  /* Size of block in bytes, because we use free list, this argument has no use	*/
);

#define PLACEHOLDER (uint32)0x12345678

#undef DEBUG_INFO
// #define DEBUG_INFO