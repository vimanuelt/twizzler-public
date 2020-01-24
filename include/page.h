#pragma once

#include <krc.h>
#include <spinlock.h>

struct page {
	uintptr_t addr;
	_Atomic int64_t mapcount;
	uint32_t flags;
	uint16_t resv;
	uint8_t type;
	uint8_t level;
	struct krc rc;
	size_t pin_count;
	struct spinlock lock;

	struct page *parent, *next;
	struct rbroot root;
	struct rbnode node;
};

#define PAGE_TYPE_VOLATILE 0
#define PAGE_TYPE_PERSIST 1
#define PAGE_TYPE_MMIO 2

#define PAGE_CACHE_TYPE(p) ((p)->flags & 0x7)
#define PAGE_CACHE_WB 0
#define PAGE_CACHE_UC 1
#define PAGE_CACHE_WT 2
#define PAGE_CACHE_WC 3

#define PAGE_PINNED 0x8
#define PAGE_ALLOCED 0x10

struct page *page_alloc(int type, int level);
struct page *page_alloc_nophys(void);
void page_unpin(struct page *page);
void page_pin(struct page *page);
void page_init(struct memregion *region);
void page_init_bootstrap(void);
