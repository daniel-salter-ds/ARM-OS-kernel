/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "console.h"

/* The following functions are special-case versions of a) writing, and 
 * b) reading a string from the UART (the latter case returning once a 
 * carriage return character has been read, or a limit is reached).
 */

void puts( char* x, int n ) {
  for( int i = 0; i < n; i++ ) {
    PL011_putc( UART1, x[ i ], true );
  }
}

void gets( char* x, int n ) {
  for( int i = 0; i < n; i++ ) {
    x[ i ] = PL011_getc( UART1, true );
    
    if( x[ i ] == '\x0A' ) {
      x[ i ] = '\x00'; break;
    }
  }
}

/* Since we lack a *real* loader (as a result of also lacking a storage
 * medium to store program images), the following function approximates 
 * one: given a program name from the set of programs statically linked
 * into the kernel image, it returns a pointer to the entry point.
 */

extern void main_P3(); 
extern void main_P4(); 
extern void main_P5(); 
extern void main_philosophers();

void* load( char* x ) {
  if     ( 0 == strcmp( x, "P3" ) ) {
    return &main_P3;
  }
  else if( 0 == strcmp( x, "P4" ) ) {
    return &main_P4;
  }
  else if( 0 == strcmp( x, "P5" ) ) {
    return &main_P5;
  }
  else if( 0 == strcmp( x, "Ph" ) ) {
    return &main_philosophers;
  }

  return NULL;
}

/* The behaviour of a console process can be summarised as an infinite 
 * loop over three main steps, namely
 *
 * 1. write a command prompt then read a command,
 * 2. tokenize command, then
 * 3. execute command.
 *
 * As is, the console only recognises the following commands:
 *
 * a. execute <program name>
 *
 *    This command will use fork to create a new process; the parent
 *    (i.e., the console) will continue as normal, whereas the child
 *    uses exec to replace the process image and thereby execute a
 *    different (named) program.  For example,
 *    
 *    execute P3
 *
 *    would execute the user program named P3.
 *
 * b. terminate <process ID> 
 *
 *    This command uses kill to send a terminate or SIG_TERM signal
 *    to a specific process (identified via the PID provided); this
 *    acts to forcibly terminate the process, vs. say that process
 *    using exit to terminate itself.  For example,
 *  
 *    terminate 3
 *
 *    would terminate the process whose PID is 3.
 */

void main_console() {
  while( 1 ) {
    char cmd[ MAX_CMD_CHARS ];

    // step 1: write command prompt, then read command.

    puts( "console $ ", 8 ); gets( cmd, MAX_CMD_CHARS );

    // step 2: tokenize command.

    int cmd_argc = 0; char* cmd_argv[ MAX_CMD_ARGS ];

    for( char* t = strtok( cmd, " " ); t != NULL; t = strtok( NULL, " " ) ) {
      cmd_argv[ cmd_argc++ ] = t;
    }

    // step 3: execute command.

    if     ( 0 == strcmp( cmd_argv[ 0 ], "execute"   ) ) {
      void* addr = load( cmd_argv[ 1 ] );

      if( addr != NULL ) {
        if( 0 == fork() ) {
          exec( addr );
        }
      }
      else {
        puts( "unknown program\n", 16 );
      }
    } 
    else if( 0 == strcmp( cmd_argv[ 0 ], "terminate" ) ) {
      kill( atoi( cmd_argv[ 1 ] ), SIG_TERM );
    } 
    else {
      puts( "unknown command\n", 16 );
    }
  }

  exit( EXIT_SUCCESS );
}
