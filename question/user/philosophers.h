/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#ifndef __philosophers_H
#define __philosophers_H

#define NUM_PHILOSOPHERS 16

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <time.h>
#include <string.h>

#include "libc.h"

typedef enum {
    REQUESTED_CHOPSTICK,
    HOLDING_CHOPSTICK,
    IDLE
} philosopherChopstickStatus;

#endif
