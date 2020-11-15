/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"

/* The kernel boots, running the console process by default; this console 
 * enables the execution of a selection of user programs and the termination
 * of any running processes. It manages running processes by:
 * 
 * - allocating a fixed-size process table (of PCBs), and then maintaining an
 *   index into it to keep track of the currently executing process.
 * - facilitating a processor context switch between executing and other
 *   saved processes, selected by a scheduling algorithm.
 * - the handling of reset, IRQ and SVC interrupt signals
 * 
 * The kernel is also responsible for the storage and management of a file
 * system, monitored using a central open file table. Each process is provided
 * with its own independant file descriptor table, pointing to its open files.
 * 
 * The creation of unnamed pipes is also handled by the kernel, facilitating
 * IPC. Pipes manifest as a buffer, stored as a file and referenced using
 * file descriptors.
 */

// Initialize global variables and declare arrays and pointers
int currentProcesses = 0;
uint32_t time = 0;

pcb_t procTab[MAX_PROCS];
fd_t openFileTab[MAX_FDS];

pcb_t *executing = NULL;

extern void main_console();
extern uint32_t tos_console;
extern uint32_t tos_p;

// Print an n character string to the terminal
void print(char *x, int n)
{
  for (int i = 0; i < n; i++)
  {
    PL011_putc(UART0, x[i], true);
  }
}

//  Print a PID (0-99) to the terminal
void printPID(int pid)
{
  int units = pid % 10;
  if (pid >= 10)
  {
    int tens = (pid - units) / 10;
    PL011_putc(UART0, '0' + tens, true);
  }
  PL011_putc(UART0, '0' + units, true);
}

// Context switch from the previous to the next process, print [prev->next]
void dispatch(ctx_t *ctx, pcb_t *prev, pcb_t *next)
{
  PL011_putc(UART0, '[', true);

  if (NULL != prev)
  {
    memcpy(&prev->ctx, ctx, sizeof(ctx_t)); // preserve execution context of P_{prev}
    printPID(prev->pid);
  }
  else
  {
    PL011_putc(UART0, '?', true);
  }

  PL011_putc(UART0, '-', true);
  PL011_putc(UART0, '>', true);

  if (NULL != next)
  {
    memcpy(ctx, &next->ctx, sizeof(ctx_t)); // restore execution context of P_{next}
    printPID(next->pid);
  }
  else
  {
    PL011_putc(UART0, '?', true);
  }

  PL011_putc(UART0, ']', true);

  executing = next; // update executing process to P_{next}

  return;
}

/* Scheduling algorithm
*  considers all eligible processes and selects the one to be run 
*  next based on a series of factors:
*
*  - Is it the currently executing process?
*  - The base priority of the process
*  - The time since its last execution
*/
void schedule(ctx_t *ctx)
{
  int prevIndex = executing->pid;
  int nextIndex = executing->pid;                // default next = currently executing
  int highestPriority = executing->niceness - 1; // favour against re-selecting currently executing process

  for (int i = 0; i < MAX_PROCS; i++)
  {
    if (procTab[i].status == STATUS_READY)
    {
      double ipriority = (time - procTab[i].lastExec) - procTab[i].niceness; // priority + time since last exec
      if (ipriority >= highestPriority)
      {
        highestPriority = ipriority;
        nextIndex = i;
      }
    }
  }

  dispatch(ctx, executing, &procTab[nextIndex]); // context switch previous -> next

  procTab[prevIndex].lastExec = time;
  if (procTab[prevIndex].status == STATUS_EXECUTING)
    procTab[prevIndex].status = STATUS_READY;   // update execution status of previous process
  procTab[nextIndex].status = STATUS_EXECUTING; // update execution status of next process

  time++;

  return;
}

// Reset interrupt handler
void hilevel_handler_rst(ctx_t *ctx)
{
  PL011_putc(UART0, 'R', true);

  TIMER0->Timer1Load = 0x00100000;  // select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl = 0x00000002;  // select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040; // select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020; // enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080; // enable          timer

  GICC0->PMR = 0x000000F0;         // unmask all            interrupts
  GICD0->ISENABLER1 |= 0x00000010; // enable timer          interrupt
  GICC0->CTLR = 0x00000001;        // enable GIC interface
  GICD0->CTLR = 0x00000001;        // enable GIC distributor

  int_enable_irq();

  /* Invalidate all entries in the process table, so it's clear they are not
   * representing valid (i.e., active) processes.
   */
  for (int i = 0; i < MAX_PROCS; i++)
  {
    procTab[i].status = STATUS_INVALID;
  }

  //initialise open file table
  for (int i = 0; i < MAX_FDS; i++)
  {
    if (i < 3)
    {
      openFileTab[i].refCount = 1;
      if (i == 0)
        openFileTab[i].flag = RDONLY;
      else
        openFileTab[i].flag = WRONLY;
    }
    else
      openFileTab[i].refCount = 0;
  }

  /* Automatically execute the user programs P1 and P2 by setting the fields
   * in two associated PCBs.  Note in each case that
   *    
   * - the CPSR value of 0x50 means the processor is switched into USR mode, 
   *   with IRQ interrupts enabled, and
   * - the PC and SP values match the entry point and top of stack. 
   */
  memset(&procTab[0], 0, sizeof(pcb_t)); // initialise 0-th PCB = console
  procTab[0].pid = 0;
  procTab[0].status = STATUS_READY;
  procTab[0].tos = (uint32_t)(&tos_console);
  procTab[0].ctx.cpsr = 0x50;
  procTab[0].ctx.pc = (uint32_t)(&main_console);
  procTab[0].ctx.sp = procTab[0].tos;
  procTab[0].lastExec = time;
  procTab[0].niceness = 0;
  for (int i = 0; i < MAX_FDS; i++)
    procTab[0].fdTab[i] = -1;

  currentProcesses++;

  /* Once the PCB has been initialised, we select the 0-th PCB (console) to be 
   * executed: there is no need to preserve the execution context, since it 
   * is invalid on reset (i.e., no process was previously executing).
   */

  dispatch(ctx, NULL, &procTab[0]);

  return;
}

// Interrupt request handler
void hilevel_handler_irq(ctx_t *ctx)
{
  // Read  the interrupt identifier so we know the source.
  uint32_t id = GICC0->IAR;

  // Handle the interrupt, then clear (or reset) the source.
  if (id == GIC_SOURCE_TIMER0)
  {
    TIMER0->Timer1IntClr = 0x01;
    schedule(ctx);
  }

  // Write the interrupt identifier to signal we're done.
  GICC0->EOIR = id;

  return;
}

// Allocate memory and fd to file
int open_fd(pipe_t *p, int flag)
{
  int fd = -1;

  for (int i = 3; i < MAX_FDS; i++)
  {
    if (openFileTab[i].refCount == 0) // file not open
    {
      fd = i;

      // add pipe to open file table
      openFileTab[fd].file = p;
      openFileTab[fd].flag = flag;
      openFileTab[fd].refCount++;

      // add pipe to process' fd table
      for (int j = 0; j < MAX_FDS; j++)
      {
        if (executing->fdTab[j] < 0) // table entry unused
        { 
          executing->fdTab[j] = fd;
          break;
        }
      }

      break;
    }
  }

  return fd;
}

// Make file descriptor and, if no longer needed, file's allocated memory available
int close_fd(int fd, pid_t pid)
{
  int r = -1; // fd index out of bounds

  if (fd >= 0 && fd < MAX_FDS)
  {
    // wipe the process' corresponding file descriptor
    for (int i = 0; i < MAX_FDS; i++)
    {
      if (procTab[pid].fdTab[i] == fd)
        procTab[pid].fdTab[i] = -1;
    }

    // update file reference count
    openFileTab[fd].refCount--;

    // free file data if no descriptors for it remain
    if (openFileTab[fd].refCount <= 0)
      free(openFileTab[fd].file);

    r = 0; // success

  }

  return r;
}

// Supervisor call handler
void hilevel_handler_svc(ctx_t *ctx, uint32_t id)
{
  /* Based on the identifier (i.e., the immediate operand) extracted from the
   * svc instruction, 
   *
   * - read  the arguments from preserved usr mode registers,
   * - perform whatever is appropriate for this system call, then
   * - write any return value back to preserved usr mode registers.
   */

  switch (id)
  {
  case 0x00: // 0x00 => yield()
  {
    schedule(ctx);

    break;
  }

  case 0x01: // 0x01 => write( fd, x, n )
  {
    int fd = (int)(ctx->gpr[0]);
    char *x = (char *)(ctx->gpr[1]);
    int n = (int)(ctx->gpr[2]);

    if (fd < 0)
    {
      print("\nERR: cannot address negative fd", 32);
      ctx->gpr[0] = -1;
    }
    else
    {
      switch (fd)
      {
      case 0: //stdin
      {
        ctx->gpr[0] = 0;
        break;
      }

      case 1: //stdout
      {
        for (int i = 0; i < n; i++)
          PL011_putc(UART0, *x++, true);
        ctx->gpr[0] = n;
        break;
      }

      case 2: //stderr
      {
        print("\nwrite error", 12);
        ctx->gpr[0] = -1;
        break;
      }

      default: // write from x to pipe at fd
      {
        // the pipe's buffer is implemented as a circular queue
        pipe_t *pipe = openFileTab[fd].file;
        int i = 0;
        for (; i < n; i++)
        {
          if (pipe->full)
            break;
          pipe->rear = (pipe->rear + 1) % pipe->size;
          pipe->buffer[pipe->rear] = *x;
          x++;
          if (pipe->front == (pipe->rear + 1) % pipe->size) // check if queue full
          {
            pipe->full = true;
          }
        }
        ctx->gpr[0] = i;
        break;
      }
      }
    }

    break;
  }

  case 0x02: // 0x02 => read( fd, x, n )
  {
    int fd = (int)(ctx->gpr[0]);
    char *x = (char *)(ctx->gpr[1]);
    int n = (int)(ctx->gpr[2]);

    if (fd < 0)
    {
      print("\nERR: cannot address negative fd", 32);
      ctx->gpr[0] = -1;
    }
    else
    {
      switch (fd)
      {
      case 0: //stdin
      {
        //scan from console
        print("\nread stdin", 11);
        ctx->gpr[0] = 0; // success
        break;
      }

      case 1: //stdout
      {
        print("\nread stdout", 12);
        ctx->gpr[0] = 0; // success
        break;
      }

      case 2: //stderr
      {
        print("\nread error", 11);
        ctx->gpr[0] = -1; // error
        break;
      }

      default: //read from pipe at fd into x
      {
        // the pipe's buffer is implemented as a circular queue
        pipe_t *pipe = openFileTab[fd].file;
        int i = 0;
        for (; i < n; i++)
        {
          int front = pipe->front;

          if ((front == (pipe->rear + 1) % pipe->size) && !pipe->full) // check queue empty
            break;
          *(x + i) = pipe->buffer[front];
          pipe->front = (front + 1) % pipe->size;
          if (pipe->full)
            pipe->full = false;
        }
        ctx->gpr[0] = i;
        break;
      }
      }
    }

    break;
  }

  case 0x03: // 0x03 => fork()
  {
    PL011_putc(UART0, 'F', true);

    if (currentProcesses >= MAX_PROCS) // process table full
    {
      print("\nERR: process table full", 24);

      ctx->gpr[0] = -1;
    }

    else
    {
      int iNew = currentProcesses;
      currentProcesses++;

      // search for a terminated process in the table
      for (int i = 1; i < MAX_PROCS; i++)
      {
        if (procTab[i].status == STATUS_TERMINATED)
        {
          iNew = i;
          break;
        }
      }

      memset(&procTab[iNew], 0, sizeof(pcb_t)); // initialise 0-th PCB

      procTab[iNew].pid = (pid_t)(iNew);
      procTab[iNew].status = STATUS_READY;
      procTab[iNew].tos = (uint32_t)(&tos_p) - (iNew - 1) * 0x00002000;

      memcpy(&procTab[iNew].ctx, ctx, sizeof(ctx_t)); // replicate state of parent - copy execution context

      // set child stack pointer to same height as parent's stack pointer
      uint32_t stackHeight = executing->tos - executing->ctx.sp;
      procTab[iNew].ctx.sp = procTab[iNew].tos - stackHeight;
      memcpy( (uint32_t*) (procTab[ iNew ].ctx.sp), (uint32_t*) ctx->sp , stackHeight);

      procTab[iNew].lastExec = time;                  // time counter reset
      procTab[iNew].niceness = executing->niceness;   // copy parent niceness

      // copy parent fd table, update open file table reference counts
      for (int i = 0; i < MAX_FDS; i++)
      {
        int fd = executing->fdTab[i];
        procTab[iNew].fdTab[i] = fd;
        if (fd >= 0)
          openFileTab[fd].refCount++;
      }

      ctx->gpr[0] = procTab[iNew].pid; // parent return value = child PID
      procTab[iNew].ctx.gpr[0] = 0;    // child return value = 0
    }

    break;
  }

  case 0x04: // 0x04 => exit( x )
  {
    PL011_putc(UART0, 'X', true);

    int x = (int)ctx->gpr[0];

    // close fds
    for (int i = 0; i < MAX_FDS; i++)
    {
      int    fd = executing->fdTab[i];
      pid_t pid = executing->pid;
      if (fd >= 0)
        close_fd(fd, pid);
    }

    executing->status = STATUS_TERMINATED;
    currentProcesses--;
    schedule(ctx);

    break;
  }

  case 0x05: // 0x05 => exec( x )
  {
    PL011_putc(UART0, 'E', true);

    ctx->pc = (uint32_t)(ctx->gpr[0]); // replace process image
    ctx->sp = executing->tos;          // reset stack pointer

    break;
  }

  case 0x06: // 0x06 => kill( pid, x )
  {
    PL011_putc(UART0, 'K', true);

    pid_t pid = (pid_t)ctx->gpr[0];
    int x = (int)ctx->gpr[1];

    // close fds
    for (int i = 0; i < MAX_FDS; i++)
    {
      int    fd = procTab[pid].fdTab[i];
      if (fd >= 0)
        close_fd(fd, pid);
    }

    procTab[pid].status = STATUS_TERMINATED;
    currentProcesses--;

    ctx->gpr[0] = 0;

    break;
  }

  case 0x07: // 0x07 => nice( pid, x )
  {
    PL011_putc(UART0, 'N', true);

    pid_t pid = (pid_t)ctx->gpr[0];
    int x = (int)ctx->gpr[1];

    if (x < -19)
      x = -19;
    else if (x > 20)
      x = 20;

    procTab[pid].niceness = x;

    ctx->gpr[0] = x;

    break;
  }

  case 0x08: // 0x08 => pipe( pipedes[2] )
  {
    int *pipedes = (int *)ctx->gpr[0];

    pipe_t *p = malloc(sizeof(pipe_t)); // initialise pipe struct
    p->front = 0;
    p->rear = -1;
    p->size = sizeof(p->buffer);
    p->full = false;

    int fd_read = open_fd(p, RDONLY); // open read end

    int fd_write = open_fd(p, WRONLY); // open write end

    if (fd_read == -1 || fd_write == -1) // pipe creation failed
    {
      print("\npipe failed", 12);
      pid_t pid = executing->pid;
      if (fd_read >= 0)
        close_fd(fd_read, pid);
      if (fd_write >= 0)
        close_fd(fd_write, pid);

      ctx->gpr[0] = -1; // failure
    }

    else
    {
      int pipefds[2];
      pipefds[0] = fd_read;
      pipefds[1] = fd_write;
      memcpy(pipedes, pipefds, 2 * sizeof(int)); // return fd indices

      ctx->gpr[0] = 0; // success
    }

    break;
  }

  case 0x09: // close( fd )
  {
    int fd = (int)ctx->gpr[0];

    pid_t pid = executing->pid;

    ctx->gpr[0] = close_fd(fd, pid);

    break;
  }

  default: // 0x?? => unknown/unsupported
  {
    break;
  }
  }

  return;
}
