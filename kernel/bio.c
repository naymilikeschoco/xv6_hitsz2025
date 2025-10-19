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


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

int
hash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKETS;
}

struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

void
binit(void)
{
  struct buf *b;

  // 初始化所有哈希桶的锁和链表头
  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    // 初始化每个桶的空链表
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  // 将所有缓冲区分配到哈希桶中
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    // 初始状态下，所有缓冲区都没有关联具体块，可以分配到任意桶
    // 均匀分配到各个桶中
    int bucket = (b - bcache.buf) % NBUCKETS;
    
    // 将缓冲区插入到对应桶的链表中
    b->next = bcache.hashbucket[bucket].next;
    b->prev = &bcache.hashbucket[bucket];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[bucket].next->prev = b;
    bcache.hashbucket[bucket].next = b;
    
    // 初始化缓冲区状态
    b->dev = 0;
    b->blockno = 0;
    b->valid = 0;
    b->refcnt = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int target_bucket = hash(dev, blockno);

  acquire(&bcache.lock[target_bucket]);

  // 1. 先在目标桶中查找是否已缓存
  for(b = bcache.hashbucket[target_bucket].next; 
      b != &bcache.hashbucket[target_bucket]; 
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[target_bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 2. 未找到，先在目标桶中寻找可回收缓冲区
  for(b = bcache.hashbucket[target_bucket].prev; 
      b != &bcache.hashbucket[target_bucket]; 
      b = b->prev){
    if(b->refcnt == 0) {
      // 找到可回收缓冲区
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[target_bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 3. 目标桶没有可用缓冲区，需要跨桶查找
  release(&bcache.lock[target_bucket]);

  // 按顺序搜索其他桶（避免死锁）
  for(int i = 0; i < NBUCKETS; i++) {
    int current_bucket = (target_bucket + i) % NBUCKETS;
    acquire(&bcache.lock[current_bucket]);

    // 跳过已经检查过的目标桶（但需要重新检查，因为可能其他线程释放了缓冲区）
    if(current_bucket == target_bucket) {
      // 重新检查目标桶（可能其他线程刚刚释放了缓冲区）
      for(b = bcache.hashbucket[target_bucket].prev; 
          b != &bcache.hashbucket[target_bucket]; 
          b = b->prev){
        if(b->refcnt == 0) {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          release(&bcache.lock[current_bucket]);
          acquiresleep(&b->lock);
          return b;
        }
      }
      release(&bcache.lock[current_bucket]);
      continue;
    }

    // 在其他桶中查找可回收缓冲区
    for(b = bcache.hashbucket[current_bucket].prev; 
        b != &bcache.hashbucket[current_bucket]; 
        b = b->prev){
      if(b->refcnt == 0) {
        // 找到可回收缓冲区，需要将其移动到目标桶
        // 从当前桶移除
        b->next->prev = b->prev;
        b->prev->next = b->next;
        
        // 重新获取目标桶锁（按固定顺序避免死锁）
        if(target_bucket < current_bucket) {
          release(&bcache.lock[current_bucket]);
          acquire(&bcache.lock[target_bucket]);
          acquire(&bcache.lock[current_bucket]);
        } else {
          acquire(&bcache.lock[target_bucket]);
        }
        
        // 插入到目标桶头部
        b->next = bcache.hashbucket[target_bucket].next;
        b->prev = &bcache.hashbucket[target_bucket];
        bcache.hashbucket[target_bucket].next->prev = b;
        bcache.hashbucket[target_bucket].next = b;
        
        // 设置新属性
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        release(&bcache.lock[current_bucket]);
        release(&bcache.lock[target_bucket]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[current_bucket]);
  }
  
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = hash(b->dev, b->blockno);

  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 移动到桶的MRU位置（头部）
    // 从当前位置移除
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // 插入到头部
    b->next = bcache.hashbucket[bucket].next;
    b->prev = &bcache.hashbucket[bucket];
    bcache.hashbucket[bucket].next->prev = b;
    bcache.hashbucket[bucket].next = b;
  }
  release(&bcache.lock[bucket]);
}

void
bpin(struct buf *b) {
  int bucket = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket]);
  b->refcnt++;
  release(&bcache.lock[bucket]);
}

void
bunpin(struct buf *b) {
  int bucket = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  release(&bcache.lock[bucket]);
}


