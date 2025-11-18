// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];

void kinit() {
  static char locknames[NCPU][16];

  for (int i = 0; i < NCPU; i++)
    snprintf(locknames[i], sizeof(locknames[i]), "kmem_%d", i);

  for (int i = 0; i < NCPU; i++) {
    initlock(&kmems[i].lock, locknames[i]);
  }
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (int i = 0; p + PGSIZE <= (char *)pa_end; i++, p += PGSIZE) {
    memset(p, 1, PGSIZE);
    struct run *r = (struct run *)p;
    struct kmem *k = &kmems[i % NCPU];

    acquire(&k->lock);
    r->next = k->freelist;
    k->freelist = r;
    release(&k->lock);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  push_off();
  int cpu_id = cpuid();
  pop_off();

  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  push_off();
  int cpu_id = cpuid();
  pop_off();

  struct run *r;

  for (int i = 0; i < NCPU; i++) {
    struct kmem *k = &kmems[(cpu_id + i) % NCPU];
    acquire(&k->lock);
    r = k->freelist;
    if (r)
      k->freelist = r->next;
    release(&k->lock);

    if (r) {
      memset((char *)r, 5, PGSIZE);
      break;
    }
  }

  return (void *)r;
}
