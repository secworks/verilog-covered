/*!
 \file     fsm.c
 \author   Trevor Williams  (trevorw@charter.net)
 \date     3/31/2002
 
 \par How are FSMs handled?
 In some fashion (either by manual input or automatic FSM extraction), an FSM state
 variable is named.  When the parser parses a module that contains this state variable,
 the size of the state variable is used to construct a two-dimensional state transition
 table which is the size of the state variable squared.  Each element of the table is
 one bit in size.  If the bit is set in an element, it is known that the state variable
 transitioned from row value to column value.  The table can be output to the CDD file
 in any way that uses the least amount of space.
 
 \par What information can be extracted from an FSM?
 Because of the history saving nature of the FSM table, at least two basic statistics
 can be drawn from it.  The first is basically, "Which states did the state machine get
 to?".  This information can be calculated by parsing the table for set bits.  When a set
 bit is found, both the row and column values are reported as achieved states.
 
 \par
 The second statistic that can be drawn from a state machine table are state transitions.
 This statistic answers the question, "Which state transition arcs in the state transition
 diagram were traversed?".  The table format is formulated to specifically calculate the
 answer to this question.  When a bit is found to be set in the table, we know which
 state (row) transitioned to which other state (column).
 
 \par What is contained in this file?
 This file contains the functions necessary to perform the following:
 -# Create the required FSM table and attach it to a signal
 -# Set the appropriate bit in the table during simulation
 -# Write/read an FSM to/from the CDD file
 -# Generate FSM report output
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <assert.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "defines.h"
#include "fsm.h"
#include "util.h"
#include "link.h"
#include "vector.h"
#include "arc.h"


extern mod_inst*    instance_root;
extern mod_link*    mod_head;
extern bool         report_covered; 
extern unsigned int report_comb_depth;
extern bool         report_instance;
extern char         leading_hierarchy[4096];
extern char         user_msg[USER_MSG_LENGTH];

/*!
 Pointer to the head of the list of FSM scopes in the design.  To extract an FSM, the user must
 specify the scope to the FSM state variable of the FSM to extract.  When the parser finds
 this signal in the design, the appropriate FSM is created and initialized.  As a note, we
 may make the FSM extraction more automatic (smarter) in the future, but we will always allow
 the user to make these choices with the -F option to the score command.
*/
fsm_var* fsm_var_head = NULL;

/*!
 Pointer to the tail of the list of FSM scopes in the design.
*/
fsm_var* fsm_var_tail = NULL;


/*!
 \param mod   String containing module containing FSM state variable.
 \param var1  String containing name of FSM input state variable in mod.
 \param var2  String containing name of FSM output state variable in mod.

 Adds the specified Verilog hierarchical scope to a list of FSM scopes to
 find during the parsing phase.
*/
void fsm_add_fsm_variable( char* mod, char* var1, char* var2 ) {

  fsm_var* new_var;  /* Pointer to newly created FSM variable */

  new_var        = (fsm_var*)malloc_safe( sizeof( fsm_var ) );
  new_var->mod   = strdup( mod );
  new_var->ivar  = strdup( var1 );
  new_var->ovar  = strdup( var2 );
  new_var->isig  = NULL;
  new_var->table = NULL;
  new_var->next  = NULL;

  if( fsm_var_head == NULL ) {
    fsm_var_head = fsm_var_tail = new_var;
  } else {
    fsm_var_tail->next = new_var;
    fsm_var_tail       = new_var;
  }

}

/*!
 \param mod  Name of current module being parsed.
 \param var  Name of current signal being created.

 \return Returns pointer to found fsm_var structure containing matching signal;
         otherwise, returns NULL.

 Checks FSM variable list to see if any entries in this list match the
 specified mod and var values.  If an entry is found to match, return a
 pointer to the found structure to indicate to the calling function that
 the specified signal needs to have an FSM structure associated with it.
*/
fsm_var* fsm_is_fsm_in_variable( char* mod, char* var ) {

  fsm_var* curr;  /* Pointer to current FSM variable element in list */

  curr = fsm_var_head;
  while( (curr != NULL) && ((strcmp( mod, curr->mod ) != 0) || (strcmp( var, curr->ivar ) != 0)) ) {
    curr = curr->next;
  }

  return( curr );

}

/*!
 \param mod  Name of current module being parsed.
 \param var  Name of current signal being created.

 \return Returns pointer to found fsm_var structure containing matching
         signal; otherwise, returns NULL.

 Checks FSM variable list to see if any entries in this list match the
 specified mod and var values.  If an entry is found to match, return a
 pointer to the found structure to indicate to the calling function that
 the specified signal needs to have an FSM structure associated with it.
*/
fsm_var* fsm_is_fsm_out_variable( char* mod, char* var ) {

  fsm_var* curr;  /* Pointer to current FSM variable element in list */

  curr = fsm_var_head;
  while( (curr != NULL) && ((strcmp( mod, curr->mod ) != 0) || (strcmp( var, curr->ovar ) != 0)) ) { 
    curr = curr->next;
  }

  return( curr );

}

/*!
 \param fv  Pointer to FSM variable structure to remove from global list.

 Searches global FSM variable list for matching FSM variable structure.
 When match is found, remove the structure and deallocate it from memory
 being sure to keep the global list intact.
*/
void fsm_var_remove( fsm_var* fv ) {

  fsm_var* curr;  /* Pointer to current FSM variable structure in list */
  fsm_var* last;  /* Pointer to last FSM variable structure evaluated  */

  /* Find matching FSM variable structure */
  curr = fsm_var_head;
  last = NULL;
  while( (curr != NULL) && (curr != fv) ) {
    last = curr;
    curr = curr->next;
  }

  /* If a matching FSM variable structure was found, remove it from the global list. */
  if( curr != NULL ) {
    if( (curr == fsm_var_head) && (curr == fsm_var_tail) ) {
      fsm_var_head = fsm_var_tail = NULL;
    } else if( curr == fsm_var_head ) {
      fsm_var_head = curr->next;
    } else if( curr == fsm_var_tail ) {
      fsm_var_tail = last;
    } else {
      last->next = curr->next;
    }
    free_safe( curr );
  }

}

/**************************************************************************************/

/*!
 \param isig  Pointer to signal that is input state variable for this FSM.
 \param osig  Pointer to signal that is output state variable for this FSM.

 \return Returns a pointer to the newly allocated FSM structure.

 Allocates and initializes an FSM structure.
*/
fsm* fsm_create( signal* sig ) {

  fsm* table;  /* Pointer to newly created FSM */

  table           = (fsm*)malloc_safe( sizeof( fsm ) );
  table->from_sig = NULL;
  table->to_sig   = sig;
  table->arc_head = NULL;
  table->arc_tail = NULL;
  table->table    = arc_create( sig->value->width );

  return( table );

}

/*!
 \param table       Pointer to FSM structure to add new arc to.
 \param from_state  Pointer to from_state expression to add.
 \param to_state    Pointer to to_state expression to add.

 Adds new FSM arc structure to specified FSMs arc list.
*/
void fsm_add_arc( fsm* table, expression* from_state, expression* to_state ) {

  fsm_arc* arc;  /* Pointer to newly created FSM arc structure */

  /* Create an initialize specified arc */
  arc             = (fsm_arc*)malloc_safe( sizeof( fsm_arc ) );
  arc->from_state = from_state;
  arc->to_state   = to_state;
  arc->next       = NULL;

  /* Add new arc to specified FSM structure */
  if( table->arc_head == NULL ) {
    table->arc_head = table->arc_tail = arc;
  } else {
    table->arc_tail->next = arc;
    table->arc_tail       = arc;
  }

}

/*!
 \param table  Pointer to FSM structure to set table sizes to.

 After the FSM signals are sized, this function is called to size
 an FSM structure (allocate memory for its tables) and the associated
 FSM arc list is parsed, setting the appropriate bit in the valid table.
*/
void fsm_create_tables( fsm* table ) {

  fsm_arc* curr_arc;    /* Pointer to current FSM arc structure       */
  bool     set = TRUE;  /* Specifies if specified bit was set         */
  int      from_state;  /* Integer value of the from_state expression */
  int      to_state;    /* Integer value of the to_state expression   */
  int      index;       /* Index of table entry to set                */
  nibble   value;       /* Bit within index to set in table entry     */
  int      i;           /* Loop iterator                              */

  /* Set valid table */
  curr_arc = table->arc_head;
  while( (curr_arc != NULL) && set ) {

    /* Evaluate from and to state expressions */
    expression_operate( curr_arc->from_state );
    expression_operate( curr_arc->to_state   );

    /* Set table entry in table, if possible */
    arc_add( &(table->table), table->to_sig->value->width, curr_arc->from_state->value, curr_arc->to_state->value, 0 );

    curr_arc = curr_arc->next;

  } 

}

/*!
 \param table  Pointer to FSM structure to output.
 \param file   Pointer to file output stream to write to.

 \return Returns TRUE if file writing is successful; otherwise, returns FALSE.

 Outputs the contents of the specified FSM to the specified CDD file.
*/
bool fsm_db_write( fsm* table, FILE* file ) {

  bool retval = TRUE;  /* Return value for this function */
  int  i;              /* Loop iterator                  */

  fprintf( file, "%d %s %s ",
    DB_TYPE_FSM,
    table->from_sig->name,
    table->to_sig->name
  );

  /* Print set table */
  arc_db_write( table->table, file );

  fprintf( file, "\n" );

  return( retval );

} 

/*!
 \param line  Pointer to current line being read from the CDD file.
 \param mod   Pointer to current module.

 \return Returns TRUE if read was successful; otherwise, returns FALSE.

 Reads in contents of FSM line from CDD file and stores newly created
 FSM into the specified module.
*/
bool fsm_db_read( char** line, module* mod ) {

  bool      retval = TRUE;    /* Return value for this function                     */
  signal    isig;             /* Temporary signal used for finding state variable   */
  signal    osig;             /* Temporary signal used for finding state variable   */
  sig_link* isigl;            /* Pointer to found state variable                    */
  sig_link* osigl;            /* Pointer to found state variable                    */
  char      isig_name[4096];  /* Temporary string used to find FSM's state variable */
  char      osig_name[4096];  /* Temporary string used to find FSM's state variable */
  int       i;                /* Loop iterator                                      */
  int       chars_read;       /* Number of characters read from sscanf              */
  fsm*      table;            /* Pointer to newly created FSM structure from CDD    */
 
  if( sscanf( *line, "%s %s%n", isig_name, osig_name, &chars_read ) == 2 ) {

    *line = *line + chars_read + 1;

    /* Find specified signal */
    isig.name = isig_name;
    osig.name = osig_name;
    if( ((isigl = sig_link_find( &isig, mod->sig_head )) != NULL) &&
        ((osigl = sig_link_find( &osig, mod->sig_head )) != NULL) ) {

      /* Create new FSM */
      table             = fsm_create( osigl->sig );
      table->from_sig   = isigl->sig;
      osigl->sig->table = table;
      fsm_create_tables( table );

      /* Now read in set table */
      if( arc_db_read( &(table->table), line ) ) {

        /* Add fsm to current module */
        fsm_link_add( table, &(mod->fsm_head), &(mod->fsm_tail) );
 
      } else {

        print_output( "Unable to read FSM state transition arc array", FATAL );
        retval = FALSE;

      }

    } else {

      snprintf( user_msg, USER_MSG_LENGTH, "Unable to find state variables (%s, %s) for current FSM", isig_name, osig_name );
      print_output( user_msg, FATAL );
      retval = FALSE;

    }

  } else {

    retval = FALSE;

  }

  return( retval );

}

/*!
 \param base  Pointer to FSM structure to merge data into.
 \param line  Pointer to read in line from CDD file to merge.
 \param same  Specifies if FSM to merge needs to be exactly the same as the existing FSM.

 \return Returns TRUE if parsing successful; otherwise, returns FALSE.

 Parses specified line for FSM information and performs merge of the base
 and in FSMs, placing the resulting merged FSM into the base signal.  If
 the FSMs are found to be unalike (names are different), an error message
 is displayed to the user.  If both FSMs are the same, perform the merge
 on the FSM's tables.
*/
bool fsm_db_merge( fsm* base, char** line, bool same ) {

  bool   retval = TRUE;  /* Return value of this function       */
  char   iname[256];      /* Name of current signal              */
  char   oname[256];      /* Name of current signal              */
  int    chars_read;     /* Number of characters read from line */
  int    i;              /* Loop iterator                       */
  nibble nib;            /* Temporary nibble storage            */

  assert( base != NULL );
  assert( base->from_sig != NULL );
  assert( base->to_sig != NULL );

  if( sscanf( *line, "%s %s%n", iname, oname, &chars_read ) == 1 ) {

    *line = *line + chars_read;

    if( (strcmp( base->from_sig->name, iname ) != 0) || (strcmp( base->to_sig->name, oname ) != 0) ) {

      print_output( "Attempting to merge two databases derived from different designs.  Unable to merge", FATAL );
      exit( 1 );

    } else {

      arc_db_merge( base->table, line, same );
          
    }

  } else {

    retval = FALSE;

  }

  return( retval );

}

/*!
 \param table  Pointer to FSM structure to set a state in.

 Taking the from and to state signal values, a new table entry is added
 to the specified FSM structure arc array (if an entry does not already
 exist in the array).
*/
void fsm_table_set( fsm* table ) {

  arc_add( &(table->table), table->to_sig->value->width, table->from_sig->value, table->to_sig->value, 1 );

}

/*!
 \param table        Pointer to FSM to get statistics from.
 \param state_total  Total number of states within this FSM.
 \param state_hit    Number of states reached in this FSM.
 \param arc_total    Total number of arcs within this FSM.
 \param arc_hit      Number of arcs reached in this FSM.

 Recursive
*/
void fsm_get_stats( fsm_link* table, float* state_total, int* state_hit, float* arc_total, int* arc_hit ) {

  fsm_link* curr;   /* Pointer to current FSM in table list             */
  int       index;  /* Current index in FSM table to evaluate           */
  nibble    value;  /* Value of current index in table to evaluate      */
  bool      hit;    /* Specifies whether the current state has been hit */
  int       i;      /* Loop iterator                                    */
  int       j;      /* Loop iterator                                    */

  curr = table;
  while( curr != NULL ) {
    arc_get_stats( curr->table->table, state_total, state_hit, arc_total, arc_hit );
    curr = curr->next;
  }

}

/*!
 \param ofile        Pointer to output file to display report contents to.
 \param root         Pointer to current root of instance tree to report.
 \param parent_inst  String containing Verilog hierarchy of this instance's parent.

 \return Returns TRUE if any FSM states/arcs were found missing; otherwise, returns FALSE.

 Generates an instance summary report of the current FSM states and arcs hit during simulation.
*/
bool fsm_instance_summary( FILE* ofile, mod_inst* root, char* parent_inst ) {

  mod_inst* curr;            /* Pointer to current child module instance of this node */
  float     state_percent;   /* Percentage of states hit                              */
  float     arc_percent;     /* Percentage of arcs hit                                */
  float     state_miss;      /* Number of states missed                               */
  float     arc_miss;        /* Number of arcs missed                                 */
  char      tmpname[4096];   /* Temporary name holder for instance                    */

  assert( root != NULL );
  assert( root->stat != NULL );

  if( root->stat->state_total == 0 ) {
    state_percent = 100.0;
  } else {
    state_percent = ((root->stat->state_hit / root->stat->state_total) * 100);
  }
  state_miss = (root->stat->state_total - root->stat->state_hit);

  if( root->stat->arc_total == 0 ) {
    arc_percent = 100.0;
  } else {
    arc_percent = ((root->stat->arc_hit / root->stat->arc_total) * 100);
  }
  arc_miss   = (root->stat->arc_total - root->stat->arc_hit);

  if( strcmp( parent_inst, "*" ) == 0 ) {
    strcpy( tmpname, root->name );
  } else {
    snprintf( tmpname, 4096, "%s.%s", parent_inst, root->name ); 
  }

  if( (root->stat->state_total == -1) || (root->stat->arc_total == -1) ) {
    fprintf( ofile, "  %-43.43s    %4d/ ???/ ???      ???%%         %4d/ ???/ ???      ???%%\n",
           tmpname,
           root->stat->state_hit,
           root->stat->arc_hit );
  } else {
    fprintf( ofile, "  %-43.43s    %4d/%4.0f/%4.0f      %3.0f%%         %4d/%4.0f/%4.0f      %3.0f%%\n",
           tmpname,
           root->stat->state_hit,
           state_miss,
           root->stat->state_total,
           state_percent,
           root->stat->arc_hit,
           arc_miss,
           root->stat->arc_total,
           arc_percent );
  }

  curr = root->child_head;
  while( curr != NULL ) {
    arc_miss = arc_miss + fsm_instance_summary( ofile, curr, tmpname );
    curr = curr->next;
  } 

  return( (state_miss > 0) || (arc_miss > 0) );

}

/*!
 \param ofile  Pointer to output file to display report contents to.
 \param head   Pointer to module list to traverse.

 \return Returns TRUE if any FSM states/arcs were found missing; otherwise, returns FALSE.

 Generates a module summary report of the current FSM states and arcs hit during simulation.
*/
bool fsm_module_summary( FILE* ofile, mod_link* head ) {

  float state_percent;       /* Percentage of states hit                        */
  float arc_percent;         /* Percentage of arcs hit                          */
  float state_miss;          /* Number of states missed                         */
  float arc_miss;            /* Number of arcs missed                           */
  bool  miss_found = FALSE;  /* Set to TRUE if state/arc was found to be missed */

  while( head != NULL ) {

    if( head->mod->stat->state_total == 0 ) {
      state_percent = 100.0;
    } else {
      state_percent = ((head->mod->stat->state_hit / head->mod->stat->state_total) * 100);
    }

    if( head->mod->stat->arc_total == 0 ) {
      arc_percent = 100.0;
    } else {
      arc_percent = ((head->mod->stat->arc_hit / head->mod->stat->arc_total) * 100);
    }

    state_miss = (head->mod->stat->state_total - head->mod->stat->state_hit);
    arc_miss   = (head->mod->stat->arc_total   - head->mod->stat->arc_hit);
    miss_found = ((state_miss > 0) || (arc_miss > 0)) ? TRUE : miss_found;

    if( (head->mod->stat->state_total == -1) || (head->mod->stat->arc_total == -1) ) {
      fprintf( ofile, "  %-20.20s    %-20.20s   %4d/ ???/ ???      ???%%         %4d/ ???/ ???      ???%%\n",
           head->mod->name,
           head->mod->filename,
           head->mod->stat->state_hit,
           head->mod->stat->arc_hit );
    } else {
      fprintf( ofile, "  %-20.20s    %-20.20s   %4d/%4.0f/%4.0f      %3.0f%%         %4d/%4.0f/%4.0f      %3.0f%%\n",
             head->mod->name,
             head->mod->filename,
             head->mod->stat->state_hit,
             state_miss,
             head->mod->stat->state_total,
             state_percent,
             head->mod->stat->arc_hit,
             arc_miss,
             head->mod->stat->arc_total,
             arc_percent );
    }

    head = head->next;

  }

  return( miss_found );

}

/*!
 \param ofile  Pointer to output file to display report contents to.
 \param root   Pointer to root of instance tree to traverse.
 \param parent_inst  String containing name of this instance's parent instance.

 Generates an instance verbose report of the current FSM states and arcs hit during simulation.
*/
void fsm_instance_verbose( FILE* ofile, mod_inst* root, char* parent_inst ) {

}

/*! 
 \param ofile  Pointer to output file to display report contents to.
 \param root   Pointer to head of module list to traverse.

 Generates a module verbose report of the current FSM states and arcs hit during simulation.
*/
void fsm_module_verbose( FILE* ofile, mod_link* mod ) {

}

/*!
 \param ofile     Pointer to file to output results to.
 \param verbose   Specifies whether or not to provide verbose information
 
 After the design is read into the module hierarchy, parses the hierarchy by module,
 reporting the FSM coverage for each module encountered.  The parent module will
 specify its own FSM coverage along with a total FSM coverage including its 
 children.
*/
void fsm_report( FILE* ofile, bool verbose ) {

  bool missed_found;  /* If set to TRUE, FSM cases were found to be missed */

  if( report_instance ) {

    fprintf( ofile, "FINITE STATE MACHINE COVERAGE RESULTS BY INSTANCE\n" );
    fprintf( ofile, "-------------------------------------------------\n" );
    fprintf( ofile, "                                                               State                             Arc\n" );
    fprintf( ofile, "Instance                                          Hit/Miss/Total    Percent hit    Hit/Miss/Total    Percent hit\n" );
    fprintf( ofile, "----------------------------------------------------------------------------------------------------------------\n" );

    missed_found = fsm_instance_summary( ofile, instance_root, leading_hierarchy );
   
    if( verbose && (missed_found || report_covered) ) {
      fsm_instance_verbose( ofile, instance_root, leading_hierarchy );
    }

  } else {

    fprintf( ofile, "FINITE STATE MACHINE COVERAGE RESULTS BY MODULE\n" );
    fprintf( ofile, "-----------------------------------------------\n" );
    fprintf( ofile, "                                                               State                             Arc\n" );
    fprintf( ofile, "Module                    Filename                Hit/Miss/Total    Percent Hit    Hit/Miss/Total    Percent hit\n" );
    fprintf( ofile, "----------------------------------------------------------------------------------------------------------------\n" );

    missed_found = fsm_module_summary( ofile, mod_head );

    if( verbose && (missed_found || report_covered) ) {
      fsm_module_verbose( ofile, mod_head );
    }

  }

  fprintf( ofile, "=================================================================================\n" );
  fprintf( ofile, "\n" );

}

/*!
 \param table  Pointer to FSM structure to deallocate.

 Deallocates all allocated memory for the specified FSM structure.
*/
void fsm_dealloc( fsm* table ) {

  fsm_arc* tmp;  /* Temporary pointer to current FSM arc structure to deallocate */

  if( table != NULL ) {

    /* Deallocate tables */
    if( table->table != NULL ) {
      free_safe( table->table );
    }

    /* Deallocate FSM arc structure */
    while( table->arc_head != NULL ) {
      tmp = table->arc_head;
      table->arc_head = table->arc_head->next;
      free_safe( tmp );
    }

    /* Deallocate this structure */
    free_safe( table );
      
  }

}

/*
 $Log$
 Revision 1.10  2003/09/13 19:53:59  phase1geo
 Adding correct way of calculating state and state transition totals.  Modifying
 FSM summary reporting to reflect these changes.  Also added function documentation
 that was missing from last submission.

 Revision 1.9  2003/09/13 02:59:34  phase1geo
 Fixing bugs in arc.c created by extending entry supplemental field to 5 bits
 from 3 bits.  Additional two bits added for calculating unique states.

 Revision 1.8  2003/09/12 04:47:00  phase1geo
 More fixes for new FSM arc transition protocol.  Everything seems to work now
 except that state hits are not being counted correctly.

 Revision 1.7  2003/08/26 21:53:23  phase1geo
 Added database read/write functions and fixed problems with other arc functions.

 Revision 1.6  2003/08/25 13:02:03  phase1geo
 Initial stab at adding FSM support.  Contains summary reporting capability
 at this point and roughly works.  Updated regress suite as a result of these
 changes.

 Revision 1.5  2002/11/23 16:10:46  phase1geo
 Updating changelog and development documentation to include FSM description
 (this is a brainstorm on how to handle FSMs when we get to this point).  Fixed
 bug with code underlining function in handling parameter in reports.  Fixing bugs
 with MBIT/SBIT handling (this is not verified to be completely correct yet).

 Revision 1.4  2002/10/29 19:57:50  phase1geo
 Fixing problems with beginning block comments within comments which are
 produced automatically by CVS.  Should fix warning messages from compiler.

 Revision 1.3  2002/09/13 05:12:25  phase1geo
 Adding final touches to -d option to report.  Adding documentation and
 updating development documentation to stay in sync.

 Revision 1.2  2002/07/03 03:31:11  phase1geo
 Adding RCS Log strings in files that were missing them so that file version
 information is contained in every source and header file.  Reordering src
 Makefile to be alphabetical.  Adding mult1.v diagnostic to regression suite.
*/

