/*!
 \file     rank.c
 \author   Trevor Williams  (phase1geo@gmail.com)
 \date     6/28/2008
*/

#include <stdio.h>
#include <assert.h>

#include "defines.h"
#include "expr.h"
#include "fsm.h"
#include "profiler.h"
#include "rank.h"
#include "util.h"
#include "vsignal.h"


extern char user_msg[USER_MSG_LENGTH];


/*!
 List of CDD filenames that need to be read in.
*/
static char** rank_in = NULL;

/*!
 Number of CDD filenames in the rank_in array.
*/
static int rank_in_num = 0;

/*!
 File to be used for outputting rank information.
*/
static char* rank_file = NULL;

/*!
 Array containing the number of coverage points for each metric for all compressed CDD coverage structures.
*/
unsigned int num_cps[CP_TYPE_NUM] = {0};


/*!
 \return Returns the number of bits that are set in the given unsigned char
*/
static inline unsigned int rank_count_bits_uchar(
  unsigned char v   /*!< Character to count bits for */
) {

  v = (v & 0x55) + ((v >> 1) & 0x55);
  v = (v & 0x33) + ((v >> 2) & 0x33);
  v = (v & 0x0f) + ((v >> 4) & 0x0f);

  return( (unsigned int)v );
  
}

/*!
 \return Returns the number of bits that are set in the given 32-bit unsigned integer.
*/
static inline unsigned int rank_count_bits_uint32(
  uint32 v  /*!< 32-bit value to count bits for */
) {

  v = (v & 0x55555555) + ((v >>  1) & 0x55555555);
  v = (v & 0x33333333) + ((v >>  2) & 0x33333333);
  v = (v & 0x0f0f0f0f) + ((v >>  4) & 0x0f0f0f0f);
  v = (v & 0x00ff00ff) + ((v >>  8) & 0x00ff00ff);
  v = (v & 0x0000ffff) + ((v >> 16) & 0x0000ffff);

  return( (unsigned int)v );

}

/*!
 \return Returns the number of bits that are set in the given 64-bit unsigned integer.
*/
static inline unsigned int rank_count_bits_uint64(
  uint64 v  /*!< 64-bit value to count bits for */
) {

  v = (v & 0x5555555555555555LL) + ((v >>  1) & 0x5555555555555555LL);
  v = (v & 0x3333333333333333LL) + ((v >>  2) & 0x3333333333333333LL);
  v = (v & 0x0f0f0f0f0f0f0f0fLL) + ((v >>  4) & 0x0f0f0f0f0f0f0f0fLL);
  v = (v & 0x00ff00ff00ff00ffLL) + ((v >>  8) & 0x00ff00ff00ff00ffLL);
  v = (v & 0x0000ffff0000ffffLL) + ((v >> 16) & 0x0000ffff0000ffffLL);
  v = (v & 0x00000000ffffffffLL) + ((v >> 32) & 0x00000000ffffffffLL);
  
  return( (unsigned int)v );

}

/*!
 \return Returns a pointer to a newly allocated and initialized compressed CDD coverage structure.
*/
comp_cdd_cov* rank_create_comp_cdd_cov(
  const char* cdd_name,   /*!< Name of CDD file that this structure was created from */
  uint64      sim_events  /*!< Number of simulation events that occurred in the CDD */
) { PROFILE(RANK_CREATE_COMP_CDD_COV);

  comp_cdd_cov* comp_cov;
  unsigned int  i;

  /* Allocate and initialize */
  comp_cov               = (comp_cdd_cov*)malloc_safe( sizeof( comp_cdd_cov* ) );
  comp_cov->cdd_name     = strdup_safe( cdd_name );
  comp_cov->sim_events   = sim_events;
  comp_cov->total_cps    = 0;
  comp_cov->unique_cps   = 0;

  for( i=0; i<CP_TYPE_NUM; i++ ) {
    comp_cov->cps_index[i] = 0;
    if( num_cps[i] > 0 ) {
      comp_cov->cps[i] = (unsigned char*)calloc_safe( ((num_cps[i] >> 3) + 1), sizeof( unsigned char ) );
    }
  }

  PROFILE_END;

  return( comp_cov );

}

/*!
 Deallocates the specified compressed CDD coverage structure.
*/
void rank_dealloc_comp_cdd_cov(
  comp_cdd_cov* comp_cov  /*!< Pointer to compressed CDD coverage structure to deallocate */
) { PROFILE(RANK_DEALLOC_COMP_CDD_COV);

  if( comp_cov != NULL ) {

    unsigned int i;

    /* Deallocate name */
    free_safe( comp_cov->cdd_name, (strlen( comp_cov->cdd_name ) + 1) );

    /* Deallocate compressed coverage point information */
    for( i=0; i<CP_TYPE_NUM; i++ ) {
      free_safe( comp_cov->cps[i], (sizeof( unsigned char* ) * ((num_cps[i] >> 3) + 1)) );
    }

    /* Now deallocate ourselves */
    free_safe( comp_cov, sizeof( comp_cdd_cov* ) );

  }

  PROFILE_END;

}

/*!
 Outputs usage information to standard output for rank command.
*/
static void rank_usage() {

  printf( "\n" );
  printf( "Usage:  covered rank [<options>] <database_to_rank> <database_to_rank>+\n" );
  printf( "\n" );
  printf( "   Options:\n" );
  printf( "      -o <filename>           Name of file to output ranking information to.  Default is stdout.\n" );
  printf( "      -h                      Displays this help information.\n" );
  printf( "\n" );

}

/*!
 \throws anonymous Throw Throw Throw

 Parses the score argument list, placing all parsed values into
 global variables.  If an argument is found that is not valid
 for the rank operation, an error message is displayed to the
 user.
*/
static void rank_parse_args(
  int          argc,      /*!< Number of arguments in argument list argv */
  int          last_arg,  /*!< Index of last parsed argument from list */
  const char** argv       /*!< Argument list passed to this program */
) {

  int i;  /* Loop iterator */

  i = last_arg + 1;

  while( i < argc ) {

    if( strncmp( "-h", argv[i], 2 ) == 0 ) {

      rank_usage();
      Throw 0;

    } else if( strncmp( "-o", argv[i], 2 ) == 0 ) {

      if( check_option_value( argc, argv, i ) ) {
        i++;
        if( rank_file != NULL ) {
          print_output( "Only one -o option is allowed on the rank command-line.  Using first value...", WARNING, __FILE__, __LINE__ );
        } else {
          if( is_legal_filename( argv[i] ) ) {
            rank_file = strdup_safe( argv[i] );
          } else {
            unsigned int rv = snprintf( user_msg, USER_MSG_LENGTH, "Output file \"%s\" is unwritable", argv[i] );
            assert( rv < USER_MSG_LENGTH );
            print_output( user_msg, FATAL, __FILE__, __LINE__ );
            Throw 0;
          }
        }
      } else {
        Throw 0;
      } 

    } else {

      /* The name of a file to rank */
      if( file_exists( argv[i] ) ) {

        /* Add the specified rank file to the list */
        rank_in              = (char**)realloc_safe( rank_in, (sizeof( char* ) * rank_in_num), (sizeof( char* ) * (rank_in_num + 1)) );
        rank_in[rank_in_num] = strdup_safe( argv[i] );
        rank_in_num++;

      } else {

        unsigned int rv = snprintf( user_msg, USER_MSG_LENGTH, "CDD file (%s) does not exist", argv[i] );
        assert( rv < USER_MSG_LENGTH );
        print_output( user_msg, FATAL, __FILE__, __LINE__ );
        Throw 0;

      }

    }

    i++;

  }

  /* Check to make sure that the user specified at least two files to rank */
  if( rank_in_num < 2 ) {
    print_output( "Must specify at least two CDD files to rank", FATAL, __FILE__, __LINE__ );
    Throw 0;
  }

  /* If -o option was not specified, default its value to stdout */
  if( rank_file == NULL ) {
    rank_file = strdup_safe( "stdout" );
  }

}

/*!
 Parses the information line of a CDD file, extracts the information that is pertanent to
 coverage ranking, and allocates/initializes a new compressed CDD coverage structure.
*/
static void rank_parse_info(
  const char*    cdd_name,  /*!< Name of the CDD file that is being parsed */
  char**         line,      /*!< Read line from CDD file to parse */
  comp_cdd_cov** comp_cov,  /*!< Reference to compressed CDD coverage structure to create */
  bool           first      /*!< If set to TRUE, populate num_cps array with found information; otherwise,
                                 verify that our coverage point numbers match the global values and error
                                 if we do not match. */
) { PROFILE(RANK_PARSE_INFO);

  unsigned int version;
  isuppl       suppl;
  uint64       sim_events;
  unsigned int i;
  int          chars_read;

  if( sscanf( *line, "%x %x %lld%n", &version, &(suppl.all), &sim_events, &chars_read ) == 3 ) {
   
    *line += chars_read;

    /* Make sure that the CDD version matches our CDD version */
    if( version != CDD_VERSION ) {
      print_output( "CDD file being read is incompatible with this version of Covered", FATAL, __FILE__, __LINE__ );
      Throw 0;
    }

    /* Parse and handle coverage point information */
    for( i=0; i<CP_TYPE_NUM; i++ ) {
      unsigned int cp_num;
      if( sscanf( *line, "%u%n", &cp_num, &chars_read ) == 1 ) {
        *line += chars_read;
        if( first ) {
          num_cps[i] = cp_num;
        } else if( num_cps[i] != cp_num ) {
          unsigned int rv = snprintf( user_msg, USER_MSG_LENGTH, "Specified CDD file \"%s\" that is not mergeable with its previous CDD files", cdd_name );
          assert( rv < USER_MSG_LENGTH );
          print_output( user_msg, FATAL, __FILE__, __LINE__ );
          Throw 0;
        }
      } else {
        print_output( "CDD file being read is incompatible with this version of Covered", FATAL, __FILE__, __LINE__ );
        Throw 0;
      }
    }

    /* Now that we have the information we need, create and populate the compressed CDD coverage structure */
    *comp_cov = rank_create_comp_cdd_cov( cdd_name, sim_events );

  } else {

    print_output( "CDD file being read is incompatible with this version of Covered", FATAL, __FILE__, __LINE__ );
    Throw 0;

  }

  PROFILE_END;

}

/*!
 Parses a signal line of a CDD file and extracts the toggle and/or memory coverage information
 from the line, compressing the coverage point information and storing it into the comp_cov
 structure.
*/
static void rank_parse_signal(
  char**        line,     /*!< Line containing signal information from CDD file */
  comp_cdd_cov* comp_cov  /*!< Pointer to compressed CDD coverage structure */
) { PROFILE(RANK_PARSE_SIGNAL);

  func_unit curr_funit;

  /* Initialize the signal list pointers */
  curr_funit.sig_head = curr_funit.sig_tail = NULL;

  /* Parse the signal */
  vsignal_db_read( line, &curr_funit );

  PROFILE_END;

}

/*!
 Parses a signal line of a CDD file and extracts the line and/or combinational logic coverage information
 from the line, compressing the coverage point information and storing it into the comp_cov
 structure.
*/
static void rank_parse_expression(
  char**        line,
  comp_cdd_cov* comp_cov
) { PROFILE(RANK_PARSE_EXPRESSION);

  func_unit curr_funit;

  /* Initialize the signal list pointers */
  curr_funit.exp_head = curr_funit.exp_tail = NULL;

  /* Parse the expression */
  expression_db_read( line, &curr_funit, FALSE );

  PROFILE_END;

}

/*!
 Parses a signal line of a CDD file and extracts the FSM state/state transition coverage information
 from the line, compressing the coverage point information and storing it into the comp_cov
 structure.
*/
static void rank_parse_fsm(
  char**        line,
  comp_cdd_cov* comp_cov
) { PROFILE(RANK_PARSE_FSM);

  func_unit curr_funit;

  /* Initialize the signal list pointers */
  curr_funit.fsm_head = curr_funit.fsm_tail = NULL;

  /* Parse the FSM */
  fsm_db_read( line, &curr_funit );

  PROFILE_END;

}

/*!
 Parses the given CDD name and stores its coverage point information in a compressed format.
*/
static void rank_read_cdd(
            const char*     cdd_name,     /*!< Filename of CDD file to read in */
            bool            first,        /*!< Set to TRUE if this if the first CDD being read */
  /*@out@*/ comp_cdd_cov*** comp_cdds,    /*!< Pointer to compressed CDD array */
  /*@out@*/ unsigned int*   comp_cdd_num  /*!< Number of compressed CDD structures in comp_cdds array */
) { PROFILE(RANK_READ_CDD);

  FILE*        ifile;           /* Pointer to CDD file handle */
  char*        curr_line;       /* Pointer to currently read CDD line */
  char*        rest_line;       /* The line that has not been parsed */
  unsigned int curr_line_size;  /* Number of bytes allocated for curr_line */
  int          type;            /* Current line type */
  int          chars_read;      /* Number of characters read from current line */

  /* Open the CDD file */
  if( (ifile = fopen( cdd_name, "r" )) != NULL ) {

    comp_cdd_cov* comp_cov = NULL;
    unsigned int  rv;

    /* Read the entire next line */
    while( util_readline( ifile, &curr_line, &curr_line_size ) ) {

      /* If the line contains a legal CDD file line, continue to parse it */
      if( sscanf( curr_line, "%d%n", &type, &chars_read ) == 1 ) {

        rest_line = curr_line + chars_read;    

        /* Determine the current information type, and if it contains coverage information, send it the proper parser */
        switch( type ) {
          case DB_TYPE_INFO       :  rank_parse_info( cdd_name, &rest_line, &comp_cov, first );  break;
          case DB_TYPE_SIGNAL     :  rank_parse_signal( &rest_line, comp_cov );      break;
          case DB_TYPE_EXPRESSION :  rank_parse_expression( &rest_line, comp_cov );  break;
          case DB_TYPE_FSM        :  rank_parse_fsm( &rest_line, comp_cov );         break;
          default                 :  break;
        }

      } else {

        rv = snprintf( user_msg, USER_MSG_LENGTH, "CDD file \"%s\" is not formatted correctly", cdd_name );
        assert( rv < USER_MSG_LENGTH );
        print_output( user_msg, FATAL, __FILE__, __LINE__ );
        Throw 0;

      }

    }

    /* Close the CDD file */
    rv = fclose( ifile );
    assert( rv == 0 );

    /* Store the new coverage point structure in the global list if it contains information */
    if( comp_cov != NULL ) {
      *comp_cdds = (comp_cdd_cov**)realloc_safe( *comp_cdds, (sizeof( comp_cdd_cov* ) * (*comp_cdd_num)), (sizeof( comp_cdd_cov* ) * ((*comp_cdd_num) + 1)) );
      (*comp_cdds)[*comp_cdd_num] = comp_cov;
      (*comp_cdd_num)++;
    }

  } else {

    unsigned int rv = snprintf( user_msg, USER_MSG_LENGTH, "Unable to read CDD file \"%s\" for ranking", cdd_name );
    assert( rv < USER_MSG_LENGTH );
    print_output( user_msg, FATAL, __FILE__, __LINE__ );
    Throw 0;

  }

  PROFILE_END;

}

/*-----------------------------------------------------------------------------------------------------------------------*/

/*!
 Performs the task of ranking the CDD files and rearranging them in the comp_cdds array such that the
 first CDD file is located at index 0.
*/
static void rank_perform(
  /*@out@*/ comp_cdd_cov** comp_cdds,    /*!< Pointer to array of compressed CDD coverage structures to rank */
            unsigned int   comp_cdd_num  /*!< Number of allocated structures in comp_cdds array */
) { PROFILE(RANK_PERFORM);

  comp_cdd_cov* merged;  /* Contains the merged results of ranked CDD files */

  /* Allocate and initialize the merged structure with the contents of the first element in comp_cdds */
  merged = rank_create_comp_cdd_cov( "merged", 0 );

  /* TBD - Perform ranking algorithm here */

  /* Deallocate merged CDD coverage structure */
  rank_dealloc_comp_cdd_cov( merged );

  PROFILE_END;

}

/*-----------------------------------------------------------------------------------------------------------------------*/

/*!
 Outputs the ranking of the CDD files to the output file specified from the rank command line.
*/
static void rank_output(
  comp_cdd_cov** comp_cdds,    /*!< Pointer to array of ranked CDD coverage structures */
  unsigned int   comp_cdd_num  /*!< Number of allocated structures in comp_cdds array */
) { PROFILE(RANK_OUTPUT);

  FILE* ofile;

  if( (ofile = fopen( rank_file, "w" )) != NULL ) {

    unsigned int rv;
    unsigned int i;
    uint64       acc_sim_events = 0;
    bool         unique_found   = TRUE;
    float        total_cps      = 0;

    /* Calculate the total number of coverage points */
    for( i=0; i<CP_TYPE_NUM; i++ ) {
      total_cps = num_cps[i];
    }

    /* TBD - Header information output */

    for( i=0; i<comp_cdd_num; i++ ) {
      acc_sim_events += comp_cdds[i]->sim_events; 
      if( (comp_cdds[i]->unique_cps == 0) && unique_found ) {
        fprintf( ofile, "\n--------------------------------  The following CDD files add no additional coverage  --------------------------------\n\n" );
        unique_found = FALSE;
      }
      fprintf( ofile, "%3.0f%%  %s  %3.0f%%  %lld  %lld\n",
               (comp_cdds[0]->unique_cps / total_cps), comp_cdds[i]->cdd_name, (comp_cdds[0]->total_cps / total_cps), comp_cdds[i]->sim_events, acc_sim_events );
    }

    /* TBD - Footer information output - if any needed */

    rv = fclose( ofile );
    assert( rv == 0 );

  } else {

    unsigned int rv = snprintf( user_msg, USER_MSG_LENGTH, "Unable to open ranking file \"%s\" for writing", rank_file );
    assert( rv < USER_MSG_LENGTH );
    print_output( user_msg, FATAL, __FILE__, __LINE__ );
    Throw 0;

  }

  PROFILE_END;

}

/*!
 \param argc      Number of arguments in command-line to parse.
 \param last_arg  Index of last parsed argument from list.
 \param argv      List of arguments from command-line to parse.

 Performs merge command functionality.
*/
void command_rank(
  int          argc,      /*!< Number of arguments in command-line to parse */
  int          last_arg,  /*!< Index of last parsed argument from list */
  const char** argv       /*!< List of arguments from command-line to parse */
) { PROFILE(COMMAND_RANK);

  int            i, j;
  unsigned int   rv;
  comp_cdd_cov** comp_cdds    = NULL;
  unsigned int   comp_cdd_num = 0;

  /* Output header information */
  rv = snprintf( user_msg, USER_MSG_LENGTH, COVERED_HEADER );
  assert( rv < USER_MSG_LENGTH );
  print_output( user_msg, NORMAL, __FILE__, __LINE__ );

  Try {

    /* Parse score command-line */
    rank_parse_args( argc, last_arg, argv );

    /* Read in databases to merge */
    for( i=0; i<rank_in_num; i++ ) {
      rv = snprintf( user_msg, USER_MSG_LENGTH, "Reading CDD file \"%s\"", rank_in[i] );
      assert( rv < USER_MSG_LENGTH );
      print_output( user_msg, NORMAL, __FILE__, __LINE__ );
      rank_read_cdd( rank_in[i], (i == 0), &comp_cdds, &comp_cdd_num );
    }

    /* Peaform the ranking algorithm */
    rank_perform( comp_cdds, comp_cdd_num );

    /* Output the results */
    rank_output( comp_cdds, comp_cdd_num );

  } Catch_anonymous {}

  /* Deallocate other allocated variables */
  for( i=0; i<rank_in_num; i++ ) {
    free_safe( rank_in[i], (strlen( rank_in[i] ) + 1) );
  }
  free_safe( rank_in, (sizeof( char* ) * rank_in_num) );

  /* Deallocate the compressed CDD coverage structures */
  for( i=0; i<comp_cdd_num; i++ ) {
    rank_dealloc_comp_cdd_cov( comp_cdds[i] );
  }
  free_safe( comp_cdds, (sizeof( comp_cdd_cov* ) * comp_cdd_num) );

  free_safe( rank_file, (strlen( rank_file ) + 1) );

  PROFILE_END;

}

/*
 $Log$
 Revision 1.1.2.1  2008/06/30 13:14:22  phase1geo
 Starting to work on new 'rank' command.  Checkpointing.

*/

