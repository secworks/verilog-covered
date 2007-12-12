/*
 Copyright (c) 2000-2007 Trevor Williams

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by the Free Software
 Foundation; either version 2 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with this program;
 if not, write to the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*!
 \file    profiler.c
 \author  Trevor Williams  (phase1geo@gmail.com)
 \date    12/10/2007
*/

#include <stdio.h>
#include <assert.h>

#include "profiler.h"


/*! Current profiling mode value */
bool profiling_mode = TRUE;

/*! Name of output profiling file */
char* profiling_output = NULL;

/*! Stack of profiles that have been called */
static unsigned int stack[4096];

/*! Current size of the profile stack */
static unsigned int stack_size = 0;



extern char user_msg[USER_MSG_LENGTH];


/*!
 \param value  New value to set profiling mode to

 Sets the current profiling mode to the given value.
*/
void profiler_set_mode( bool value ) {

  profiling_mode = value;

}

/*!
 \param fname  Name of output profiling file.

 Sets the profiling output file to the given value.
*/
void profiler_set_filename( const char* fname ) {

  /* Deallocate profiling output name, if one was already specified */
  free_safe( profiling_output );

  profiling_output = strdup_safe( fname );

}

/*!
 \param index  Profiler index of current function.

 Increases the current call count for the current function, stops the timer for
 the last running counter and starts the timer for the current function.
*/
void profiler_enter( unsigned int index ) {

  /* Stop the last running timer if we are going to be timed */
  if( (stack_size > 0) && profiles[index].timed && profiles[stack[stack_size-1]].timed ) {
    timer_stop( &profiles[stack[stack_size-1]].time_in );
  }

  /* Increment the calls counter */
  profiles[index].calls++;

  /* Start the timer for this function, if needed */
  if( profiles[index].timed ) {
    timer_start( &profiles[index].time_in );
    stack[stack_size] = index;
    stack_size++;
  }

}

/*!
 Gets called when leaving a profiling function.  Stops the current timer and pops the stack.
*/
void profiler_exit( unsigned int index ) {

  /* Stop the current timer */
  timer_stop( &profiles[index].time_in );

  /* Pop the stack */
  stack_size--;

  /* Start the timer, if needed */
  if( (stack_size > 0) && profiles[stack[stack_size-1]].timed ) {
    timer_start( &profiles[stack[stack_size-1]].time_in );
  }

}

/*!
 Deallocates all allocated memory for profiler.
*/
void profiler_dealloc() {

  int i;  /* Loop iterator */

  /* Deallocate profiling output name */
  free_safe( profiling_output );

  /* Iterate through the profiler array and deallocate all timer structures */
  for( i=0; i<NUM_PROFILES; i++ ) {
    free_safe( profiles[i].time_in );
  }

}

void profiler_display_calls( FILE* ofile ) {

  int largest;             /* Index of largest calls profile */
  int i;                   /* Loop iterator */
  int j;                   /* Loop iterator */
  int list[NUM_PROFILES];  /* List of indices that can be used to sort */
  int tmp;                 /* Used for value swapping */

  /* Prepare a list of key/value pairs */
  for( i=0; i<NUM_PROFILES; i++ ) {
    list[i] = i;
  }

  /* Display header for this section */
  fprintf( ofile, "==============================================================================\n" );
  fprintf( ofile, "=                           Function Calls Profile                           =\n" );
  fprintf( ofile, "==============================================================================\n" );
  fprintf( ofile, "\n" );
  fprintf( ofile, "This section describes the number of times each function was called\n" );
  fprintf( ofile, "during the command run.  Note that functions are ordered from the most\n" );
  fprintf( ofile, "called to the least called.\n" );
  fprintf( ofile, "\n" );
  fprintf( ofile, "------------------------------------------------------------------------------------------------------\n" );
  fprintf( ofile, "Function Name                               calls       time        avg. time   mallocs     frees\n" );
  fprintf( ofile, "------------------------------------------------------------------------------------------------------\n" );

  /* Output them in order of most to least */
  for( i=(NUM_PROFILES-1); i>=0; i-- ) {
    largest = 0;
    for( j=0; j<i; j++ ) {
      if( profiles[list[j]].calls > profiles[list[largest]].calls ) {
        largest = j;
      }
    }
    tmp           = list[j];
    list[j]       = list[largest];
    list[largest] = tmp;
    if( profiles[list[j]].calls > 0 ) {
      if( profiles[list[j]].time_in == NULL ) {
        fprintf( ofile, "  %-40.40s  %10d          NA          NA  %10d  %10d\n",
                 profiles[list[j]].func_name, profiles[list[j]].calls, profiles[list[j]].mallocs, profiles[list[j]].frees );
      } else {
        fprintf( ofile, "  %-40.40s  %10d  %10d  %10d  %10d  %10d\n",
                 profiles[list[j]].func_name, profiles[list[j]].calls, profiles[list[j]].time_in->total,
                 (profiles[list[j]].time_in->total / profiles[list[j]].calls), profiles[list[j]].mallocs, profiles[list[j]].frees );
      }
    }
  }

}

/*!
 Generates profiling report if the profiling mode is set to TRUE.
*/
void profiler_report() {

  FILE* ofile;  /* File stream pointer to output file */

  if( profiling_mode ) {

    assert( profiling_output != NULL );

    if( (ofile = fopen( profiling_output, "w" )) != NULL ) {

      profiler_display_calls( ofile );

      /* Close the output file */
      fclose( ofile );

    } else {

      snprintf( user_msg, USER_MSG_LENGTH, "Unable to open profiling output file \"%s\" for writing", profiling_output );
      print_output( user_msg, FATAL, __FILE__, __LINE__ );

    }

  }

  /* Delete memory associated with the profiler */
  profiler_dealloc();

}


/*
 $Log$
 Revision 1.3  2007/12/12 07:23:19  phase1geo
 More work on profiling.  I have now included the ability to get function runtimes.
 Still more work to do but everything is currently working at the moment.

 Revision 1.2  2007/12/11 23:19:14  phase1geo
 Fixed compile issues and completed first pass injection of profiling calls.
 Working on ordering the calls from most to least.

 Revision 1.1  2007/12/11 15:07:57  phase1geo
 Adding missing file.

 Revision 1.1  2007/12/10 23:16:22  phase1geo
 Working on adding profiler for use in finding performance issues.  Things don't compile
 at the moment.

*/

