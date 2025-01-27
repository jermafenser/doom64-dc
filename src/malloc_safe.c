#include <stdlib.h>
#include <stdint.h>

#include <arch/arch.h>
#include <arch/memory.h>
#include <arch/mmu.h>
#include <kos/mutex.h>

static mmucontext_t *ctx;
static unsigned int virtpage_counter;
static mutex_t lock = MUTEX_INITIALIZER;

static inline int ptr_to_page(void *ptr)
{
	return ((uintptr_t)ptr & MEM_AREA_CACHE_MASK) >> PAGESIZE_BITS;
}

static inline void *page_to_ptr(int page)
{
	return (void *)((uintptr_t)page << PAGESIZE_BITS | MEM_AREA_P1_BASE);
}

int malloc_safe_init(void)
{
	mmu_init();

	ctx = mmu_context_create(0);
	if (!ctx)
		return -1;

	mmu_use_table(ctx);
	mmu_switch_context(ctx);

	return 0;
}

void * malloc_safe(size_t size)
{
	size_t nb_pages;
	void *ptr;

	mutex_lock_scoped(&lock);

	ptr = aligned_alloc(4096, size);
	if (!ptr)
		return NULL;

	nb_pages = (size + 4095) / 4096;
	virtpage_counter++;

	mmu_page_map(ctx, virtpage_counter, ptr_to_page(ptr),
		     nb_pages, MMU_KERNEL_RDWR,
		     MMU_CACHEABLE, 0, 0);

	ptr = (void *)(virtpage_counter << PAGESIZE_BITS);

	virtpage_counter += nb_pages;

	return ptr;
}

void free_safe(void *ptr)
{
	int ret;

	mutex_lock_scoped(&lock);

	ret = mmu_virt_to_phys(ctx, (uintptr_t)ptr >> PAGESIZE_BITS);
	if (ret >= 0)
		free(page_to_ptr(ret));
}
