// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "buf.h"
#include "defs.h"
#include "fs.h"
#include "param.h"
#include "riscv.h"
#include "sleeplock.h"
#include "spinlock.h"
#include "types.h"

#define NBUCKETS 13

struct {
  struct spinlock locks[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf heads[NBUCKETS];
} bcache;

void binit(void) {
  struct buf *b;

  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.locks[i], "bcache");
    bcache.heads[i].prev = &bcache.heads[i];
    bcache.heads[i].next = &bcache.heads[i];
  }

  // Create linked list of buffers
  for (int i = 0; i < NBUF; i++) {
    b = &bcache.buf[i];
    int bucketno = i % NBUCKETS;
    b->next = bcache.heads[bucketno].next;
    b->prev = &bcache.heads[bucketno];
    initsleeplock(&b->lock, "buffer");
    bcache.heads[bucketno].next->prev = b;
    bcache.heads[bucketno].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;

  int bucketno = blockno % NBUCKETS;
  acquire(&bcache.locks[bucketno]);

  // Is the block already cached?
  for (b = bcache.heads[bucketno].next; b != &bcache.heads[bucketno];
       b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.locks[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (b = bcache.heads[bucketno].prev; b != &bcache.heads[bucketno];
       b = b->prev) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.locks[bucketno]);
  for (int i = 1; i < NBUCKETS; i++) {
    int bn = (bucketno + i) % NBUCKETS;
    acquire(&bcache.locks[bn]);
    for (b = bcache.heads[bn].prev; b != &bcache.heads[bn]; b = b->prev) {
      if (b->refcnt == 0) {
        // remove from old bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.locks[bn]);

        // add to new bucket
        acquire(&bcache.locks[bucketno]);
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        b->next = bcache.heads[bucketno].next;
        b->prev = &bcache.heads[bucketno];
        bcache.heads[bucketno].next->prev = b;
        bcache.heads[bucketno].next = b;

        release(&bcache.locks[bucketno]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.locks[bn]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  int bucketno = b->blockno % NBUCKETS;
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.locks[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.heads[bucketno].next;
    b->prev = &bcache.heads[bucketno];
    bcache.heads[bucketno].next->prev = b;
    bcache.heads[bucketno].next = b;
  }

  release(&bcache.locks[bucketno]);
}

void bpin(struct buf *b) {
  int bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.locks[bucketno]);
  b->refcnt++;
  release(&bcache.locks[bucketno]);
}

void bunpin(struct buf *b) {
  int bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.locks[bucketno]);
  b->refcnt--;
  release(&bcache.locks[bucketno]);
}
