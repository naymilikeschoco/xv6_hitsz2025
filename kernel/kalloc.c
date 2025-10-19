// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void
kinit()
{
  // 初始化每个CPU的锁
  for (int id = 0; id < NCPU; id++) {
    initlock(&kmems[id].lock, "kmem");
    kmems[id].freelist = 0;  //初始化为空
  }

  uint64 start_addr = (uint64)end;
  uint64 total_pages = (PHYSTOP - start_addr) / PGSIZE;
  uint64 pages_per_cpu = total_pages / NCPU;

  for (int i = 0; i < NCPU; i++) {
    uint64 cpu_start = start_addr + i * pages_per_cpu * PGSIZE;
    uint64 cpu_end;
    
    if (i == NCPU - 1) {
      cpu_end = PHYSTOP;
    } else {
      cpu_end = cpu_start + pages_per_cpu * PGSIZE;
      cpu_end = PGROUNDDOWN(cpu_end);
    }
    
    // 确保起始地址对齐
    cpu_start = PGROUNDUP(cpu_start);

    // 初始化该CPU的内存池
    for (uint64 p = cpu_start; p + PGSIZE <= cpu_end; p += PGSIZE) {
      // 确保不重复初始化同一个页面
      struct run *r = (struct run*)p;
      acquire(&kmems[i].lock);
      r->next = kmems[i].freelist;
      kmems[i].freelist = r;
      release(&kmems[i].lock);
    }
  }
  
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //在调用cpuid()并使用其返回值的过程中需要关闭中断。
  push_off();
  int cpu_id = cpuid();
  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu_id;

  push_off();
  cpu_id = cpuid();

  // 先尝试当前CPU的内存池
  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;
  if(r) {
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
    pop_off();
    
    if(r)
      memset((char*)r, 5, PGSIZE);
    return (void*)r;
  }
  release(&kmems[cpu_id].lock);  // 重要：当前CPU为空时要释放锁

  // 当前CPU为空，尝试其他CPU
  for (int i = 0; i < NCPU; i++) {
    if (i == cpu_id) continue;
    
    acquire(&kmems[i].lock);
    r = kmems[i].freelist;
    if (r) {
      kmems[i].freelist = r->next;
      release(&kmems[i].lock);
      pop_off();
      
      if(r)
        memset((char*)r, 5, PGSIZE);
      return (void*)r;
    }
    release(&kmems[i].lock);
  }
  pop_off();
  // 所有内存池都为空
  return 0;
}