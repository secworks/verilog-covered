#ifndef __SIM_H__
#define __SIM_H__

/*!
 \file     sim.h
 \author   Trevor Williams  (trevorw@charter.net)
 \date     6/20/2002
 \brief    Contains functions for simulation engine.
*/


#include "defines.h"


/*! \brief Adds specified expression's statement to pre-simulation statement queue. */
void sim_expr_changed( expression* expr );

/*! \brief Adds specified statement to pre-simulation statement queue. */
void sim_add_stmt_to_queue( statement* stmt );

void sim_set_curr_wait_signals();

void sim_clear_curr_wait_signals();

/*! \brief Searches pre-simulation queue for specified signal name. */
bool sim_is_curr_wait_signal( signal* sig );

/*! \brief Adds static expression values to initial simulator */
void sim_add_statics();

/*! \brief Simulates current timestep. */
void sim_simulate();


/*
 $Log$
 Revision 1.9  2003/08/15 03:52:22  phase1geo
 More checkins of last checkin and adding some missing files.

 Revision 1.8  2003/08/05 20:25:05  phase1geo
 Fixing non-blocking bug and updating regression files according to the fix.
 Also added function vector_is_unknown() which can be called before making
 a call to vector_to_int() which will eleviate any X/Z-values causing problems
 with this conversion.  Additionally, the real1.1 regression report files were
 updated.

 Revision 1.7  2002/11/27 03:49:20  phase1geo
 Fixing bugs in score and report commands for regression.  Finally fixed
 static expression calculation to yield proper coverage results for constant
 expressions.  Updated regression suite and development documentation for
 changes.

 Revision 1.6  2002/11/05 00:20:08  phase1geo
 Adding development documentation.  Fixing problem with combinational logic
 output in report command and updating full regression.

 Revision 1.5  2002/10/31 23:14:25  phase1geo
 Fixing C compatibility problems with cc and gcc.  Found a few possible problems
 with 64-bit vs. 32-bit compilation of the tool.  Fixed bug in parser that
 lead to bus errors.  Ran full regression in 64-bit mode without error.

 Revision 1.4  2002/10/29 19:57:51  phase1geo
 Fixing problems with beginning block comments within comments which are
 produced automatically by CVS.  Should fix warning messages from compiler.

 Revision 1.3  2002/06/25 21:46:10  phase1geo
 Fixes to simulator and reporting.  Still some bugs here.

 Revision 1.2  2002/06/22 21:08:23  phase1geo
 Added simulation engine and tied it to the db.c file.  Simulation engine is
 currently untested and will remain so until the parser is updated correctly
 for statements.  This will be the next step.

 Revision 1.1  2002/06/21 05:55:05  phase1geo
 Getting some codes ready for writing simulation engine.  We should be set
 now.
*/

#endif

