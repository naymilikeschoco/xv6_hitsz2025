#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int flags; //为1时不阻塞否则阻塞
  if (argaddr(0, &p) < 0) return -1;
  // 获取第二个参数（整数）
  if (argint(1, &flags) < 0) return -1;
  return wait(p, flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

uint64 sys_yield(void) {
  struct proc* p = myproc();

 // 计算上下文保存的地址区间
  uint64 start_addr = (uint64)(&(p->context));
  uint64 end_addr = start_addr + sizeof(struct context);
  
  printf("Save the context of the process to the memory region from address %p to %p\n", 
         start_addr, end_addr);

  printf("Current running process pid is %d and user pc is %p\n", p->pid, p->trapframe->epc);

  //环形遍历全局进程表找到下一个RUNNABLE进程
  acquire(&p->lock);
  struct proc *next;
  int start_idx = p->pid;
  for (int i = 1; i < NPROC; i++) {
    int idx = (start_idx + i) % NPROC;
    if (proc[idx].state == RUNNABLE && &proc[idx] != p) {
      next = &proc[idx];
      printf("Next runnable process pid is %d and user pc is %p\n", next->pid, next->trapframe->epc);
      break;
    }
  }
  release(&p->lock);
  yield();
  return 0;
}