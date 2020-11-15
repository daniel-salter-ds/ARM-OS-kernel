/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "philosophers.h"

/* The Dining Philosophers Problem:
 * n number of Philosophers sit around a cirular table with a bowl of rice in 
 * front of each. There is one chopstick on the table between each pair of 
 * philosophers. They are all hungry but cannot eat until they hold a chopstick
 * in each hand. The philosophers are unable to communicate with eachother.
 * 
 * To avoid deadlock a waiter decides when it is OK for a philosopher to pick
 * up their chopsticks. 
 * To avoid any of the philosophers starving the waiter is strategic with the 
 * order in which he chooses to communicate with them, allowing philosophers who
 * have eaten least recently first access to the chopsticks.
 */

void writePhilosoperID(int id)
{
    write(STDOUT_FILENO, "\nPhilosopher ", 13);

    id++;
    char id_str[3];
    itoa(id_str, id);
    if (id < 10)
        write(STDOUT_FILENO, id_str, 1);
    else
        write(STDOUT_FILENO, id_str, 2);

    write(STDOUT_FILENO, " ", 1);

    return;
}

void think(int id)
{
    writePhilosoperID(id);
    write(STDOUT_FILENO, "is thinking", 11);

    return;
}

bool requestChopsticks(int id, int fd_write)
{
    int n = write(fd_write, "R", 1); // Request chopsticks from waiter

    writePhilosoperID(id);
    write(STDOUT_FILENO, "request chopsticks", 18);

    return n;
}

int getWaiterReply(int id, int fd_read)
{
    char reply[1] = "X";

    int i = read(fd_read, reply, 1);

    return (i == 1) + (i == 1 && reply[0] == 'Y'); // Return waiter's answer
}

void eat(int id)
{
    writePhilosoperID(id);
    write(STDOUT_FILENO, "is eating", 9);

    return;
}

bool putDownChopsticks(int id, int fd_write)
{
    int n = write(fd_write, "P", 1); //Tell waiter putting chopsticks down

    writePhilosoperID(id);
    write(STDOUT_FILENO, "putting chopsticks down", 23);

    return n;
}

void philosopher(int id, int fd_read, int fd_write)
{
    philosopherChopstickStatus status = IDLE;
    while (1)
    {
        think(id);

        if (status == IDLE)
        {
            if (requestChopsticks(id, fd_write))
                status = REQUESTED_CHOPSTICK;
            yield();
        }

        switch (getWaiterReply(id, fd_read))
        {
        case 0: // no reply from waiter
        {
            yield();
            break;
        }
        case 1: // chopsticks unavailable
        {
            status = IDLE;
            break;
        }
        case 2: // chopsticks available
        {
            writePhilosoperID(id);
            write(STDOUT_FILENO, "picking chopsticks up", 21);
            status = HOLDING_CHOPSTICK;
            eat(id);
            break;
        }
        }

        if (status == HOLDING_CHOPSTICK)
            if (putDownChopsticks(id, fd_write))
                status = IDLE;
    }
}

void main_philosophers()
{
    write(STDOUT_FILENO, "\nPhilosophers start", 19);

    int fd_waiterRead[NUM_PHILOSOPHERS];
    int fd_waiterWrite[NUM_PHILOSOPHERS];

    int fd_philosopherRead;
    int fd_philosopherWrite;

    int priority[NUM_PHILOSOPHERS]; // stores number of meals each Philosopher has eaten
    int maxPriority = 0;            // minimum meals eaten

    bool chopstickFree[NUM_PHILOSOPHERS];
    for (int i = 0; i < NUM_PHILOSOPHERS; i++)
    {
        chopstickFree[i] = true;
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++)
    {
        //initialise pipes
        int WtoP_pipedes[2];
        int PtoW_pipedes[2];

        int e = 0;
        e += pipe(WtoP_pipedes); // create pipe waiter->philosopher
        e += pipe(PtoW_pipedes); // create pipe philosopher->waiter
        if (e < 0)
        {
            write(STDOUT_FILENO, "\nERROR: pipe failed", 19);
            exit(EXIT_FAILURE);
        }

        fd_waiterRead[i] = PtoW_pipedes[0];
        fd_waiterWrite[i] = WtoP_pipedes[1];
        fd_philosopherRead = WtoP_pipedes[0];
        fd_philosopherWrite = PtoW_pipedes[1];

        int pid = fork();
        if (pid == -1)
        {
            write(STDOUT_FILENO, "\nERROR: fork failed", 19);
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        { // child => philospher
            // close unneeded ends of pipes
            for (int j = 0; j <= i; j++)
            {
                close(fd_waiterWrite[j]);
                close(fd_waiterRead[j]);
            }

            //increase priority of philosopher
            nice(pid, -1);

            philosopher(i, fd_philosopherRead, fd_philosopherWrite);
        }
        else
        { // parent => waiter
            // close unneeded ends of pipes
            close(fd_philosopherRead);
            close(fd_philosopherWrite);
        }
    }

    yield();

    // parent => waiter
    while (1)
    {
        write(STDOUT_FILENO, "\nWaiter", 7);
        // print_fds();

        // clear table

        // choose next philosopher to serve
        int ph_served = 0;
        int p = maxPriority;
        maxPriority++;
        while (ph_served < NUM_PHILOSOPHERS)
        {
            for (int id = 0; id < NUM_PHILOSOPHERS; id++)
            {
                if (priority[id] == p) //serve philosopher id
                {
                    // handle chopstick pick up/put down requests
                    char r[1] = "X";
                    int n = read(fd_waiterRead[id], r, 1); // read message from philosopher
                    // writePhilosoperID(id);
                    if (n == 1)
                    {
                        if (r[0] == 'R') // philosopher requesting chopsticks
                        {
                            // check if both chopsticks free
                            if (chopstickFree[id] && chopstickFree[(id + 1) % NUM_PHILOSOPHERS])
                            {
                                // allow chopstick pickup
                                int n = write(fd_waiterWrite[id], "Y", 1);
                                if (n == 1)
                                {
                                    // update chopstick state
                                    chopstickFree[id] = false;
                                    chopstickFree[(id + 1) % NUM_PHILOSOPHERS] = false;
                                    // update priority
                                    priority[id]++;
                                }
                            }
                            else
                            {
                                // deny chopstick pickup
                                write(fd_waiterWrite[id], "N", 1);
                            }
                        }

                        else if (r[0] == 'P') // philosopher putting down chopsticks
                        {
                            // update chopstick state
                            chopstickFree[id] = true;
                            chopstickFree[(id + 1) % NUM_PHILOSOPHERS] = true;
                        }

                        else
                        {
                            writePhilosoperID(id);
                            write(STDOUT_FILENO, "\nERROR: not valid request", 25);
                            exit(EXIT_FAILURE);
                        }
                    }
                    ph_served++;
                    if (priority[id] < maxPriority)
                        maxPriority--;
                }
            }
            p++;
        }
        // print_fds();
        yield();
    }

    exit(EXIT_SUCCESS);
}
