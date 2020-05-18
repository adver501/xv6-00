#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = QTM;
  while(QTM - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&QTM, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = QTM;
  release(&tickslock);
  return xticks;
}

int
sys_getyear(void)
{
  return getyear();
}

int
sys_getchildren(void)
{
  return getchildren(myproc()->pid);
}

int
sys_changepolicy(void)
{
  changepolicy();
  return 0;
}

int
sys_changepriority(void)
{
  int pid, proy;
  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &proy) < 0)
    return -1;
  
  return changepriority(pid, proy);
}

int
sys_waitForChiled(void)
{
  int *watime;
  int *rutime;
  
  if(argptr(0, (char**)&watime, sizeof(int)) < 0)
    return 12;

  if(argptr(1, (char**)&rutime, sizeof(int)) < 0)
    return 13;

  return waitForChiled(watime,rutime);
}

int
sys_trace(void)
{
  int x;
  if(argint(0, &x) < 0){
    return -1; // error
  }
  if(x == 0){
    myproc()->printSys = 0;
  }else{
    myproc()->printSys = 1;
  }
  return myproc()->sysCalls;
}