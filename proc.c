#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  // // Three queues, associating with each priority level
  // struct proc* que[3][NPROC];
  // //three numbers, associating with the number of processes in each queue
  // int priCount[3];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int wake[] = {0,0,0};

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  // ptable.priCount[0] = -1;
  // ptable.priCount[1] = -1;
  // ptable.priCount[2] = -1;
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority = 1;  //default priority
  p->changablePriority = 0;  //equal to priority at first(Default)
  p->queueIndex = 0;
  int i;
  for (i=0; i<3; i++)
  {
    p->ticks[i] = 0;
  }
  // p->currTicks = 0;
  wake[0] = 1;
  p->crtime = ticks;
  p->tetime = 0;
  p->rutime = 0;
  p->sltime=0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // p->queueIndex = 0;
  // p->crtime = ticks;
  // p->tetime = 0;
  // p->rutime = 0;
  // p->sltime=0;

  p->printSys = 0;
  p->sysCalls = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  // ptable.priCount[0]++;
  // ptable.que[0][ptable.priCount[0]] = p;

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  // ptable.priCount[0]++;
  // ptable.que[0][ptable.priCount[0]] = np;

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  curproc->tetime = ticks;

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
	int limit[] = {5,10,20};
	int count = 0;
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
	int qindex;
	for(qindex = 0; qindex < 3; qindex++) {
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			int i;
			if(qindex != 0) {
				
				for(i = 0; i<qindex;i++) {
					if(wake[i]) {
						wake[i] = 0;
						qindex = i-1;
						break;
					}
				}
					
			}
			if(p->state != RUNNABLE)
        continue;
			if(p->queueIndex == qindex) {
				

				c->proc = p;
				int j;
               
				for(j = p->changablePriority; j <limit[qindex]; j++) {
          if(qindex != 0) {
            for(i = 0; i<qindex;i++) {
              if(wake[i]) 
                break;
            
            }
          }
					
      		switchuvm(p);
      		p->state = RUNNING;
      		swtch(&(c->scheduler), p->context);
      		switchkvm();
				
					p->ticks[qindex]++;
          p->changablePriority += p->priority;
                   
					// p->ran = 1;
					count ++;
					
					if(count == 100) {
							struct proc *tmp;
							for(tmp = ptable.proc; tmp < &ptable.proc[NPROC]; tmp++){
								if(tmp-> state != RUNNABLE || tmp->queueIndex == 0 /*|| tmp->ran ==1*/ ) {
									// tmp->ran = 0;
								  continue;
								}
								if(tmp->queueIndex == 3) {

									}
								tmp->queueIndex --;
								tmp->changablePriority = 0;
								if(tmp->queueIndex < qindex) {
									wake[tmp->queueIndex] = 1;
								}
							}
							count = 0;
					}
					if(p->state != RUNNABLE) {

						break;
					}
				}
				
				if(j == limit[qindex]) {
						if(p->queueIndex <2) {
							p->queueIndex++;
						}
            p->changablePriority = 0;
				}
			}

				c->proc = 0;
		}
			
	}	
    
			

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      

    release(&ptable.lock);

/*  struct proc *p;
  // struct proc *p1;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      // cprintf("pid: %s // ", p->state);
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
*/
/*
    struct proc *highp;

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      // ptable.priCount[0]++;
      // ptable.que[0][ptable.priCount[0]] = p;
      cprintf("pid: %s // ", p->state);
      
    }
    
    int queueIndex;
    for(queueIndex = 0; queueIndex <= PRIORITY_MAX; queueIndex++) {
      int j = 0;
      cprintf( "queueIndex: %d\n priCount: %d\n", queueIndex, ptable.priCount[queueIndex] );
      while(ptable.priCount[queueIndex] >= -1) {
        if(j == ptable.priCount[queueIndex]){
          break;
        }

        // if(ptable.que[queueIndex][j]->state != RUNNABLE)
        //   continue;
        
        j++;
        highp = ptable.que[queueIndex][0];
        int lasti = 0;
        int i;
        
        for(i = 0; i < ptable.priCount[queueIndex]; i++){
          // if(ptable.que[queueIndex][i]->state != RUNNABLE || ptable.que[queueIndex][i]->queueIndex != queueIndex){
          //   continue;
          // }
          p1 = ptable.que[queueIndex][i];
          if(highp->changablePriority > p1->changablePriority){
            highp = p1;
            lasti = i;
          }
        }
        
        p = highp;

        ptable.que[queueIndex][lasti] = ptable.que[queueIndex][0];
        ptable.que[queueIndex][0] = p;
        
        for (i = 0; i < ptable.priCount[queueIndex]; i++) {
          ptable.que[queueIndex][i] = ptable.que[queueIndex][i + 1];
        }
        ptable.priCount[queueIndex]--;
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(c->proc);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // queueIndex = 0;
        if (c->proc->queueIndex < 2)
          c->proc->queueIndex++;
        ptable.priCount[c->proc->queueIndex]++;
        ptable.que[c->proc->queueIndex][ptable.priCount[c->proc->queueIndex]] = c->proc;

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
    }
*/
    // release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock

  // if (myproc()->queueIndex < 2)
  //   myproc()->queueIndex++;
  // ptable.priCount[myproc()->queueIndex]++;
  // ptable.que[myproc()->queueIndex][ptable.priCount[myproc()->queueIndex]] = myproc();

  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){

      // ptable.priCount[p->queueIndex]++;
      // ptable.que[p->queueIndex][ptable.priCount[p->queueIndex]] = p;

      p->state = RUNNABLE;

      wake[p->queueIndex] = 1;
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        
        // ptable.priCount[p->queueIndex]++;
        // ptable.que[p->queueIndex][ptable.priCount[p->queueIndex]] = p;
        
        p->state = RUNNABLE;
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
cps()
{
  struct proc *p;

  sti();

  acquire(&ptable.lock);
  cprintf("name   \t   pid   \t   state   \t   priority\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == SLEEPING)
      cprintf("%s   \t   %d   \t   SLEEPING   \t   %d\n ", p->name, p->pid, p->priority);
    else if(p->state == RUNNING)
      cprintf("%s   \t   %d   \t   RUNNING   \t   %d\n ", p->name, p->pid, p->priority);
  }

  release(&ptable.lock);

  return 22;
  
  
}

int
changepriority(int pid, int priority)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid){
      p->priority = priority;
      break;
    }
  }
  release(&ptable.lock);

  return pid;
  
}

int
getyear(void)
{
  return 2020;
}

int
getchildren(int pid)
{
    struct proc *p;
    int count = 0;
    char ch[16];

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent->pid == pid){ 
        count++;
        if(count > 1){
          // cprintf(0);
          // cprintf("%d",p->pid);
          strncpy(ch, "0", 2);
        }
        if (count == 1)
        {
          strncpy(ch, "children pid: ", 15);
          // cprintf("children pid:%s%d", ch, p->pid);
        }
        cprintf("%s%d", ch, p->pid);
      }
    }
    release(&ptable.lock);
    // exit();
    return count;
}

void
changepolicy(void)
{
  struct proc *p;
  struct proc *p1;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    struct proc *highp;

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      highp = p;
      for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
        if(p1->state != RUNNABLE)
          continue;
        if(highp->changablePriority > p1->changablePriority)
          highp = p1;
      }
      
      p = highp;
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

int
waitForChiled(int *watime, int *rutime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        *watime= p->tetime - p->crtime - p->rutime - p->sltime;
        *rutime=p->rutime;
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }

}

// void
// resetchangablePriority(void) {
//   struct proc *p;
//   acquire(&ptable.lock);
//   for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//       if (p->state == RUNNABLE) {
//           //delete the runnable process from its original queue
//           int token;
//           for (token = 0; token < NPROC; token++) {
//               if (p == ptable.que[p->changablePriority][token]) {
//                   int i;
//                   for (i = token; i < ptable.priCount[p->changablePriority]; i++) {
//                       ptable.que[p->changablePriority][i] = ptable.que[p->changablePriority][i + 1];
//                   }
//                   ptable.priCount[p->changablePriority]--;
//               }
//               break;
//           }
//           //set the priority to 0, and add it to the first queue
//           p->changablePriority = 0;
//           ptable.priCount[0]++;
//           ptable.que[0][ptable.priCount[0]] = p;
//       } else {
//           //queues only contain process that are runnable, so change the priority is enough.
//           p->changablePriority = 0;
//       }
//   }
//   release(&ptable.lock);
// }

