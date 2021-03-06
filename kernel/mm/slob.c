#include "mm.h"
#include "libk.h"
#include "util.h"
#include "sync.h"

#define UNIT sizeof(struct slob_header)

// Anything more than (4096 - UNIT) bytes will get it's own large_alloc slot
#define BIGALLOC (PAGE_SIZE - UNIT)

/* Slob Allocator
 *
 * This is a simple K&R style first-fit linked list allocator for general
 * purpose kernel allocations. One list is maintained for small allocations
 * and another is used for large allocations (roughly greater than a page).
 *
 * Inspired by Linux's SLOB allocator. */

static spinlock_t slob_lock;

struct slob_large_alloc {
	struct slist_node list;
	virtaddr_t addr;
	uint8_t order;
};

struct slob_header {
	struct slist_node list; // Pointer to next
	size_t units; // bytes = units * sizeof(struct slob_header)
};

struct slob_header *head;
struct slob_large_alloc *large_head;

static void morecore(void)
{
	// Grab a single page
	virtaddr_t p = page_to_virt(pmm_alloc_order(0, GFP_NONE)); // TODO: Check for fail
	struct slob_header *hd = (struct slob_header *)p;
	hd->units = (PAGE_SIZE - UNIT) / UNIT;

	slist_set_next(hd, list, head);
	head = hd;

	klog("slob", "Morecore allocated a page at %p\n", p);
}

// TODO: Worry about alignment
static virtaddr_t slob_alloc(size_t bytes, unsigned int alloc_flags)
{
	(void)alloc_flags;
	size_t units = DIV_ROUND_UP(bytes, UNIT);
	if (bytes < BIGALLOC) {
		// This should only loop at most once since our allocation is guaranteed to succeed
		// if morecore() is called. This is only true because morecore allocates at least
		// BIGALLOC bytes, and also no other allocations can occur inbetween.
		spin_lock(&slob_lock);
		while (1) {
			struct slob_header *prev = NULL;
			slist_foreach(block, list, head) {
				if (block->units == units) {
					// Exact match
					if (prev == NULL)
						head = block;
					else
						slist_set_next(prev, list, slist_get_next(block, list));
					spin_unlock(&slob_lock);
					return (virtaddr_t)(block + 1);
				} else if (block->units > units) {
					// Split a block
					struct slob_header *new_block = block + units + 1;
					new_block->units = block->units - units - 1;
					block->units = units;
					slist_set_next(new_block, list, slist_get_next(block, list));
					// Remove allocated block from list
					if (prev == NULL)
						head = new_block;
					else
						slist_set_next(prev, list, new_block);
					spin_unlock(&slob_lock);
					return (virtaddr_t)(block + 1);
				}
				prev = block;
			}
			morecore();
		}
	} else {
		panic("unimplemented");
	}
	return NULL;
}

// TODO: Defragment
static void slob_free(virtaddr_t p)
{
	if (ISALIGN_POW2((uintptr_t)p, PAGE_SIZE)) {
		// BIGALLOC bytes or more
		panic("unimplemented");
	} else {
		struct slob_header *header = (struct slob_header *)p - 1;
		spin_lock(&slob_lock);
		slist_set_next(header, list, head);
		head = header;
#ifdef DEBUG
		memset(p, 0xBB, header->units * UNIT);
#endif
		spin_unlock(&slob_lock);
	}
}

#if 0
static void slob_debug(void)
{
	slist_foreach(block, list, head) {
		kprintf("Block: %p, %zu units, %p next\n", (void *)(block + 1), block->units, (void *)(slist_get_next(block, list) + 1));
	}
}
#endif

void *kmalloc(size_t n, unsigned int alloc_flags)
{
#ifdef DEBUG
	void *p = slob_alloc(n, alloc_flags);
	memset(p, 0xBB, n);
	return p;
#else
	return slob_alloc(n, alloc_flags);
#endif
}

void kfree(void *p)
{
	slob_free(p);
}
