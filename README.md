# ARM-OS-kernel
A fully functioning operating system kernel coded from in C and assembly, capable of parallelism by multiprocessing.

The project runs on a QEMU ARM based emulator.
The kernel consists of 'lolevel', a selection of assembly routunes that manage the emulated processor's registers, and 'hilevel', with complimentary C routines as well as functions defining the ARM system calls. 

Several user programs designed to run on the processor are included as part of the project. Most notably is the 'philosphers' program: a implementation of the dining philosphers problem, it demonstrates the concurrency capabilities of the processor by forking the main process into 16 child processes that then share the processor using interrupts, intelligently managed by the scheduling algorithm.
