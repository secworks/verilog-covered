/*!
 \file     comb.c
 \author   Trevor Williams  (trevorw@charter.net)
 \date     3/31/2002
*/

#include <stdio.h>
#include <assert.h>

#include "defines.h"
#include "comb.h"
#include "codegen.h"
#include "util.h"
#include "vector.h"


extern mod_inst* instance_root;
extern mod_link* mod_head;


/*!
 \param expl   Pointer to current expression link to evaluate.
 \param total  Pointer to total number of logical combinations.
 \param hit    Pointer to number of logical combinations hit during simulation.

 Recursively traverses the specified expression list, recording the total number
 of logical combinations in the expression list and the number of combinations
 hit during the course of simulation.  An expression can be considered for
 combinational coverage if the "measured" bit is set in the expression.
*/
void combination_get_stats( exp_link* expl, float* total, int* hit ) {

  exp_link* curr_exp;    /* Pointer to the current expression link in the list */

  curr_exp = expl;

  while( curr_exp != NULL ) {
    if( EXPR_IS_MEASURABLE( curr_exp->exp ) == 1 ) {
      *total = *total + 2;
      *hit   = *hit + SUPPL_WAS_TRUE( curr_exp->exp->suppl ) + SUPPL_WAS_FALSE( curr_exp->exp->suppl );
    }
    curr_exp = curr_exp->next;
  }

}

/*!
 \param ofile   Pointer to file to output results to.
 \param root    Pointer to node in instance tree to evaluate.
 \param parent  Name of parent instance name.

 \return Returns TRUE if combinations were found to be missed; otherwise,
         returns FALSE.

 Outputs summarized results of the combinational logic coverage per module
 instance to the specified output stream.  Summarized results are printed 
 as percentages based on the number of combinations hit during simulation 
 divided by the total number of expression combinations possible in the 
 design.  An expression is said to be measurable for combinational coverage 
 if it evaluates to a value of 0 or 1.
*/
bool combination_instance_summary( FILE* ofile, mod_inst* root, char* parent ) {

  mod_inst* curr;          /* Pointer to current child module instance of this node               */
  float     percent;       /* Percentage of lines hit                                             */
  float     miss;          /* Number of lines missed                                              */

  assert( root != NULL );
  assert( root->stat != NULL );

  if( root->stat->comb_total == 0 ) {
    percent = 100;
  } else {
    percent = ((root->stat->comb_hit / root->stat->comb_total) * 100);
  }
  miss    = (root->stat->comb_total - root->stat->comb_hit);

  fprintf( ofile, "  %-20.20s    %-20.20s    %3d/%3.0f/%3.0f      %3.0f%%\n",
           root->name,
           parent,
           root->stat->comb_hit,
           miss,
           root->stat->comb_total,
           percent );

  curr = root->child_head;
  while( curr != NULL ) {
    miss = miss + combination_instance_summary( ofile, curr, root->name );
    curr = curr->next;
  }

  return( miss > 0 );

}

/*!
 \param ofile   Pointer to file to output results to.
 \param head    Pointer to link in current module list to evaluate.

 \return Returns TRUE if combinations were found to be missed; otherwise,
         returns FALSE.

 Outputs summarized results of the combinational logic coverage per module
 to the specified output stream.  Summarized results are printed as 
 percentages based on the number of combinations hit during simulation 
 divided by the total number of expression combinations possible in the 
 design.  An expression is said to be measurable for combinational coverage 
 if it evaluates to a value of 0 or 1.
*/
bool combination_module_summary( FILE* ofile, mod_link* head ) {

  float     total_lines = 0;  /* Total number of lines parsed                         */
  int       hit_lines   = 0;  /* Number of lines executed during simulation           */
  mod_inst* curr;             /* Pointer to current child module instance of this node */
  float     percent;          /* Percentage of lines hit                               */
  float     miss;             /* Number of lines missed                                */

  combination_get_stats( head->mod->exp_head, &total_lines, &hit_lines );

  if( total_lines == 0 ) {
    percent = 100;
  } else {
    percent = ((hit_lines / total_lines) * 100);
  }
  miss    = (total_lines - hit_lines);

  fprintf( ofile, "  %-20.20s    %-20.20s    %3d/%3.0f/%3.0f      %3.0f%%\n", 
           head->mod->name,
           head->mod->filename,
           hit_lines,
           miss,
           total_lines,
           percent );

  if( head->next != NULL ) {
    miss = miss + combination_module_summary( ofile, head->next );
  }

  return( miss > 0 );

}

/*!
 \param line    Pointer to line to create line onto.
 \param size    Number of characters long line is.
 \param exp_id  ID to place in underline.

 Draws an underline containing the specified expression ID to the specified
 line.
*/
void combination_draw_line( char* line, int size, int exp_id ) {

  char str_exp_id[12];   /* String containing value of exp_id        */
  int  exp_id_size;      /* Number of characters exp_id is in length */
  int  i;                /* Loop iterator                            */

  /* Calculate size of expression ID */
  snprintf( str_exp_id, 12, "%d", exp_id );
  exp_id_size = strlen( str_exp_id );

  line[0] = '|';

  for( i=1; i<((size - exp_id_size) / 2); i++ ) {
    line[i] = '-';
  }

  line[i] = '\0';
  strcat( line, str_exp_id );

  for( i=(i + exp_id_size); i<(size - 1); i++ ) {
    line[i] = '-';
  }

  line[i]   = '|';
  line[i+1] = '\0';

}

/*!
 \param exp     Pointer to expression to create underline for.
 \param lines   Stack of lines for left child.
 \param depth   Pointer to top of left child stack.
 \param size    Pointer to character width of this node.
 \param exp_id  Pointer to current expression ID to use in labeling.

 Recursively parses specified expression tree, underlining and labeling each
 measurable expression.
*/
void combination_underline_tree( expression* exp, char*** lines, int* depth, int* size, int* exp_id ) {

  char** l_lines;       /* Pointer to left underline stack              */
  char** r_lines;       /* Pointer to right underline stack             */
  int    l_depth = 0;   /* Index to top of left stack                   */
  int    r_depth = 0;   /* Index to top of right stack                  */
  int    l_size;        /* Number of characters for left expression     */
  int    r_size;        /* Number of characters for right expression    */
  int    i;             /* Loop iterator                                */
  char   exp_sp[256];   /* Space to take place of missing expression(s) */
  char   code_fmt[20];  /* Contains format string for rest of stack     */
  char*  tmpstr;        /* Temporary string value                       */
  
  *depth  = 0;
  *size   = 0;
  l_lines = NULL;
  r_lines = NULL;

  if( exp != NULL ) {

    if( SUPPL_OP( exp->suppl ) == EXP_OP_LAST ) {

      *size = 0;

    } else if( SUPPL_OP( exp->suppl ) == EXP_OP_STATIC ) {

      if( vector_get_type( exp->value ) == DECIMAL ) {

        snprintf( code_fmt, 20, "%d", vector_to_int( exp->value ) );
        *size = strlen( code_fmt );
      
      } else {

        tmpstr = vector_to_string( exp->value, vector_get_type( exp->value ) );
        *size  = strlen( tmpstr );
        free_safe( tmpstr );

      }

    } else {

      if( SUPPL_OP( exp->suppl ) == EXP_OP_SIG ) {

        *size = strlen( exp->sig->name );
        switch( *size ) {
          case 0 :  assert( *size > 0 );                     break;
          case 1 :  *size = 3;  strcpy( code_fmt, " %s " );  break;
          case 2 :  *size = 3;  strcpy( code_fmt, " %s" );   break;
          default:  strcpy( code_fmt, "%s" );                break;
        }
        
      } else {

        combination_underline_tree( exp->left,  &l_lines, &l_depth, &l_size, exp_id );
        combination_underline_tree( exp->right, &r_lines, &r_depth, &r_size, exp_id );

        switch( SUPPL_OP( exp->suppl ) ) {
          case EXP_OP_XOR      :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_MULTIPLY :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_DIVIDE   :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_MOD      :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_ADD      :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_SUBTRACT :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_AND      :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_OR       :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_NAND     :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_NOR      :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_NXOR     :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_LT       :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_GT       :  *size = l_size + r_size + 5;  strcpy( code_fmt, " %s   %s "        );  break;
          case EXP_OP_LSHIFT   :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_RSHIFT   :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_EQ       :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_CEQ      :  *size = l_size + r_size + 7;  strcpy( code_fmt, " %s     %s "      );  break;
          case EXP_OP_LE       :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_GE       :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_NE       :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_CNE      :  *size = l_size + r_size + 7;  strcpy( code_fmt, " %s     %s "      );  break;
          case EXP_OP_LOR      :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_LAND     :  *size = l_size + r_size + 6;  strcpy( code_fmt, " %s    %s "       );  break;
          case EXP_OP_COND     :  *size = l_size + r_size + 3;  strcpy( code_fmt, "%s   %s"          );  break;
          case EXP_OP_COND_SEL :  *size = l_size + r_size + 3;  strcpy( code_fmt, "%s   %s"          );  break;
          case EXP_OP_UINV     :  *size = l_size + r_size + 1;  strcpy( code_fmt, " %s"              );  break;
          case EXP_OP_UAND     :  *size = l_size + r_size + 1;  strcpy( code_fmt, " %s"              );  break;
          case EXP_OP_UNOT     :  *size = l_size + r_size + 1;  strcpy( code_fmt, " %s"              );  break;
          case EXP_OP_UOR      :  *size = l_size + r_size + 1;  strcpy( code_fmt, " %s"              );  break;
          case EXP_OP_UXOR     :  *size = l_size + r_size + 1;  strcpy( code_fmt, " %s"              );  break;
          case EXP_OP_UNAND    :  *size = l_size + r_size + 2;  strcpy( code_fmt, "  %s"             );  break;
          case EXP_OP_UNOR     :  *size = l_size + r_size + 2;  strcpy( code_fmt, "  %s"             );  break;
          case EXP_OP_UNXOR    :  *size = l_size + r_size + 2;  strcpy( code_fmt, "  %s"             );  break;
          case EXP_OP_SBIT_SEL :  
            *size = l_size + r_size + strlen( exp->sig->name ) + 2;  
            strcpy( code_fmt, "%s" );  
            break;
          case EXP_OP_MBIT_SEL :  
            *size = l_size + r_size + strlen( exp->sig->name ) + 3;  
            strcpy( code_fmt, "%s" );  
            break;
          case EXP_OP_EXPAND   :  *size = l_size + r_size + 0;  strcpy( code_fmt, "%s"               );  break;  // ???
          case EXP_OP_CONCAT   :  *size = l_size + r_size + 2;  strcpy( code_fmt, " %s "             );  break;
          case EXP_OP_LIST     :  *size = l_size + r_size + 2;  strcpy( code_fmt, "%s  %s"           );  break;
          case EXP_OP_PEDGE    :  *size = l_size + r_size + 8;  strcpy( code_fmt, "        %s"       );  break;
          case EXP_OP_NEDGE    :  *size = l_size + r_size + 8;  strcpy( code_fmt, "        %s"       );  break;
          case EXP_OP_AEDGE    :  *size = l_size + r_size + 0;  strcpy( code_fmt, "%s"               );  break;
          case EXP_OP_EOR      :  *size = l_size + r_size + 4;  strcpy( code_fmt, "%s    %s"         );  break;
          case EXP_OP_CASE     :  *size = l_size + r_size + 11; strcpy( code_fmt, "      %s   %s  "  );  break;
          case EXP_OP_CASEX    :  *size = l_size + r_size + 12; strcpy( code_fmt, "       %s   %s  " );  break;
          case EXP_OP_CASEZ    :  *size = l_size + r_size + 12; strcpy( code_fmt, "       %s   %s  " );  break;
          default              :  
            print_output( "Internal error:  Unknown expression type in combination_underline_tree", FATAL );
            exit( 1 );
            break;
        }

      }

      if( l_depth > r_depth ) {
        *depth = l_depth + EXPR_COMB_MISSED( exp );
      } else {
        *depth = r_depth + EXPR_COMB_MISSED( exp );
      }

      if( *depth > 0 ) {

        /* Allocate all memory for the stack */
        *lines = (char**)malloc_safe( sizeof( char* ) * (*depth) );

        /* Allocate memory for this underline */
        (*lines)[(*depth)-1] = (char*)malloc_safe( *size + 1 );

        /* Create underline or space */
        if( EXPR_COMB_MISSED( exp ) == 1 ) {
          combination_draw_line( (*lines)[(*depth)-1], *size, *exp_id );
          // printf( "Drawing line (%s), size: %d, depth: %d\n", (*lines)[(*depth)-1], *size, (*depth) );
          *exp_id = *exp_id + 1;
        }

        /* Combine the left and right line stacks */
        for( i=0; i<(*depth - EXPR_COMB_MISSED( exp )); i++ ) {

          (*lines)[i] = (char*)malloc_safe( *size + 1 );

          if( (i < l_depth) && (i < r_depth) ) {
         
            /* Merge left and right lines */
            snprintf( (*lines)[i], (*size + 1), code_fmt, l_lines[i], r_lines[i] );

            free_safe( l_lines[i] );
            free_safe( r_lines[i] );

          } else if( i < l_depth ) {

            /* Create spaces for right side */
            gen_space( exp_sp, r_size );

            /* Merge left side only */
            snprintf( (*lines)[i], (*size + 1), code_fmt, l_lines[i], exp_sp );
            
            free_safe( l_lines[i] );

          } else if( i < r_depth ) {

            if( l_size == 0 ) { 
 
              snprintf( (*lines)[i], (*size + 1), code_fmt, r_lines[i] );

            } else {

              /* Create spaces for left side */
              gen_space( exp_sp, l_size );

              /* Merge right side only */
              snprintf( (*lines)[i], (*size + 1), code_fmt, exp_sp, r_lines[i] );
          
            }
  
            free_safe( r_lines[i] );
   
          } else {

            print_output( "Internal error:  Reached entry without a left or right underline", FATAL );
            exit( 1 );

          }

        }

        /* Free left child stack */
        if( l_depth > 0 ) {
          free_safe( l_lines );
        }

        /* Free right child stack */
        if( r_depth > 0 ) {
          free_safe( r_lines );
        }

      }

    }

  }
    
}

/*!
 \param ofile     Pointer output stream to display underlines to.
 \param exp       Pointer to parent expression to create underline for.
 \param begin_sp  Spacing that is placed at the beginning of the generated line.

 Traverses through the expression tree that is on the same line as the parent,
 creating underline strings.  An underline is created for each expression that
 does not have complete combination logic coverage.  Each underline (children to
 parent creates an inverted tree) and contains a number for the specified expression.
*/
void combination_underline( FILE* ofile, expression* exp, char* begin_sp ) {

  char** lines;    /* Pointer to a stack of lines     */
  int    depth;    /* Depth of underline stack        */
  int    size;     /* Width of stack in characters    */
  int    exp_id;   /* Expression ID to place in label */
  int    i;        /* Loop iterator                   */

  exp_id = 1;

  combination_underline_tree( exp, &lines, &depth, &size, &exp_id );

  for( i=0; i<depth; i++ ) {
    fprintf( ofile, "%s%s\n", begin_sp, lines[i] );
    free_safe( lines[i] );
  }

  if( depth > 0 ) {
    free_safe( lines );
  }

}

/*!
 \param ofile  Pointer to file to output results to.
 \param exp    Pointer to expression to evaluate.

 Displays the missed unary combination(s) that keep the combination coverage for
 the specified expression from achieving 100% coverage.
*/
void combination_unary( FILE* ofile, expression* exp ) {

  assert( exp != NULL );

  fprintf( ofile, " Value\n" );
  fprintf( ofile, "-------\n" );
  
  if( SUPPL_WAS_FALSE( exp->suppl ) == 0 ) {
    fprintf( ofile, "   0\n" );
  }

  if( SUPPL_WAS_TRUE( exp->suppl ) == 0 ) {
    fprintf( ofile, "   1\n" );
  }

  fprintf( ofile, "\n" );

}

/*!
 \param ofile  Pointer to file to output results to.
 \param exp    Pointer to expression to evaluate.
 \param val0   When operation is evaluated, contains result when left == 0 and right == 0
 \param val1   When operation is evaluated, contains result when left == 0 and right == 1
 \param val2   When operation is evaluated, contains result when left == 1 and right == 0
 \param val3   When operation is evaluated, contains result when left == 1 and right == 1

 Displays the missed combinational sequences for the specified expression to the
 specified output stream in tabular form.
*/
void combination_two_vars( FILE* ofile, expression* exp, int val0, int val1, int val2, int val3 ) {

  /* Verify that left child expression is valid for this operation */
  assert( exp->left != NULL );

  /* Verify that right child expression is valid for this operation */
  assert( exp->right != NULL );

  fprintf( ofile, " L | R | Value\n" );
  fprintf( ofile, "---+---+------\n" );

  if( !((SUPPL_WAS_FALSE( exp->left->suppl ) == 1) && (SUPPL_WAS_FALSE( exp->right->suppl ) == 1)) ||
      !((val0 == 1) ? (SUPPL_WAS_TRUE( exp->suppl ) == 1) : (SUPPL_WAS_FALSE( exp->suppl ) == 1)) ) {
    fprintf( ofile, " 0 | 0 |    %d\n", val0 );
  }

  if( !((SUPPL_WAS_FALSE( exp->left->suppl ) == 1) && (SUPPL_WAS_TRUE( exp->right->suppl ) == 1)) ||
      !((val1 == 1) ? (SUPPL_WAS_TRUE( exp->suppl ) == 1) : (SUPPL_WAS_FALSE( exp->suppl ) == 1)) ) {

    fprintf( ofile, " 0 | 1 |    %d\n", val1 );
  }

  if( !((SUPPL_WAS_TRUE( exp->left->suppl ) == 1) && (SUPPL_WAS_FALSE( exp->right->suppl ) == 1)) ||
      !((val2 == 1) ? (SUPPL_WAS_TRUE( exp->suppl ) == 1) : (SUPPL_WAS_FALSE( exp->suppl ) == 1)) ) {

    fprintf( ofile, " 1 | 0 |    %d\n", val2 );
  }

  if( !((SUPPL_WAS_TRUE( exp->left->suppl ) == 1) && (SUPPL_WAS_TRUE( exp->right->suppl ) == 1)) ||
      !((val3 == 1) ? (SUPPL_WAS_TRUE( exp->suppl ) == 1) : (SUPPL_WAS_FALSE( exp->suppl ) == 1)) ) {
    fprintf( ofile, " 1 | 1 |    %d\n", val3 );
  }

  fprintf( ofile, "\n" );

}

/*!
 \param ofile   Pointer to file to output results to.
 \param exp     Pointer to expression tree to evaluate.
 \param exp_id  Pointer to current expression ID to use.

 Describe which combinations were not hit for all subexpressions in the
 specified expression tree.  We display the value of missed combinations by
 displaying the combinations of the children expressions that were not run
 during simulation.
*/
void combination_list_missed( FILE* ofile, expression* exp, int* exp_id ) {

  if( exp != NULL ) {
    
    combination_list_missed( ofile, exp->left,  exp_id );
    combination_list_missed( ofile, exp->right, exp_id );

    if( EXPR_COMB_MISSED( exp ) == 1 ) {

      // printf( "missed expression, op: %d, id: %d\n", SUPPL_OP( exp->suppl ), exp->id );

      fprintf( ofile, "Expression %d\n", *exp_id );
      fprintf( ofile, "^^^^^^^^^^^^^\n" );

      /* Create combination table */
      switch( SUPPL_OP( exp->suppl ) ) {
        case EXP_OP_SIG      :  combination_unary( ofile, exp );                 break;
        case EXP_OP_XOR      :  combination_two_vars( ofile, exp, 0, 1, 1, 0 );  break;
        case EXP_OP_ADD      :  combination_two_vars( ofile, exp, 0, 1, 1, 0 );  break;
        case EXP_OP_SUBTRACT :  combination_two_vars( ofile, exp, 0, 1, 1, 0 );  break;
        case EXP_OP_AND      :  combination_two_vars( ofile, exp, 0, 0, 0, 1 );  break;
        case EXP_OP_OR       :  combination_two_vars( ofile, exp, 0, 1, 1, 1 );  break;
        case EXP_OP_NAND     :  combination_two_vars( ofile, exp, 1, 1, 1, 0 );  break;
        case EXP_OP_NOR      :  combination_two_vars( ofile, exp, 1, 0, 0, 0 );  break;
        case EXP_OP_NXOR     :  combination_two_vars( ofile, exp, 1, 0, 0, 1 );  break;
        case EXP_OP_LT       :  combination_unary( ofile, exp );                 break;
        case EXP_OP_GT       :  combination_unary( ofile, exp );                 break;
        case EXP_OP_LSHIFT   :  combination_unary( ofile, exp );                 break;
        case EXP_OP_RSHIFT   :  combination_unary( ofile, exp );                 break;
        case EXP_OP_EQ       :  combination_unary( ofile, exp );                 break;
        case EXP_OP_CEQ      :  combination_unary( ofile, exp );                 break;
        case EXP_OP_LE       :  combination_unary( ofile, exp );                 break;
        case EXP_OP_GE       :  combination_unary( ofile, exp );                 break;
        case EXP_OP_NE       :  combination_unary( ofile, exp );                 break;
        case EXP_OP_CNE      :  combination_unary( ofile, exp );                 break;
        case EXP_OP_COND     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_LOR      :  combination_two_vars( ofile, exp, 0, 1, 1, 1 );  break;
        case EXP_OP_LAND     :  combination_two_vars( ofile, exp, 0, 0, 0, 1 );  break;
        case EXP_OP_UINV     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UAND     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UNOT     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UOR      :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UXOR     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UNAND    :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UNOR     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_UNXOR    :  combination_unary( ofile, exp );                 break;
        case EXP_OP_SBIT_SEL :  combination_unary( ofile, exp );                 break;
        case EXP_OP_MBIT_SEL :  combination_unary( ofile, exp );                 break;
        case EXP_OP_EXPAND   :  /* ??? */  break;
        case EXP_OP_CONCAT   :  combination_unary( ofile, exp );                 break;
        case EXP_OP_EOR      :  combination_two_vars( ofile, exp, 0, 1, 1, 1 );  break;
        case EXP_OP_CASE     :  combination_unary( ofile, exp );                 break;
        case EXP_OP_CASEX    :  combination_unary( ofile, exp );                 break;
        case EXP_OP_CASEZ    :  combination_unary( ofile, exp );                 break;        
        default              :  break;
      }
      
      *exp_id = *exp_id + 1;
      
    }

  }

}

/*!
 \param expr  Pointer to root of expression tree to search.

 Recursively traverses specified expression tree, returning TRUE
 if an expression is found that has not received 100% coverage for
 combinational logic.
*/
bool combination_missed_expr( expression* expr ) {
  
  if( expr != NULL ) {

    return( (EXPR_COMB_MISSED( expr ) == 1)       || 
            combination_missed_expr( expr->left ) || 
            combination_missed_expr( expr->right ) );
  
  } else {

    return( FALSE );

  }

}

/*!
 \param ofile  Pointer to file to output results to.
 \param stmtl  Pointer to statement list head.

 Displays the expressions (and groups of expressions) that were considered 
 to be measurable (evaluates to a value of TRUE (1) or FALSE (0) but were 
 not hit during simulation.  The entire Verilog expression is displayed
 to the specified output stream with each of its measured expressions being
 underlined and numbered.  The missed combinations are then output below
 the Verilog code, showing those logical combinations that were not hit
 during simulation.
*/
void combination_display_verbose( FILE* ofile, stmt_link* stmtl ) {

  expression* unexec_exp;      /* Pointer to current unexecuted expression    */
  char*       code;            /* Code string from code generator             */
  int         exp_id;          /* Current expression ID of missed expression  */

  fprintf( ofile, "Missed Combinations\n" );

  /* Display current instance missed lines */
  while( stmtl != NULL ) {

    if( combination_missed_expr( stmtl->stmt->exp ) ) {

      unexec_exp = stmtl->stmt->exp;
      exp_id     = 1;

      fprintf( ofile, "====================================================\n" );
      fprintf( ofile, " Line #     Expression\n" );
      fprintf( ofile, "====================================================\n" );

      /* Generate line of code that missed combinational coverage */
      code = codegen_gen_expr( unexec_exp, -1 );
      fprintf( ofile, "%7d:    %s\n", unexec_exp->line, code );

      /* Output underlining feature for missed expressions */
      combination_underline( ofile, unexec_exp, "            " );
      fprintf( ofile, "\n" );

      fprintf( ofile, "\n" );

      free_safe( code );

      /* Output logical combinations that missed complete coverage */
      combination_list_missed( ofile, unexec_exp, &exp_id );

    }

    stmtl = stmtl->next;

  }

  fprintf( ofile, "\n" );

}

/*!
 \param ofile  Pointer to file to output results to.
 \param root   Pointer to current module instance to evaluate.

 Outputs the verbose coverage report for the specified module instance
 to the specified output stream.
*/
void combination_instance_verbose( FILE* ofile, mod_inst* root ) {

  mod_inst*   curr_inst;   /* Pointer to current instance being evaluated */

  assert( root != NULL );

  fprintf( ofile, "\n" );
  fprintf( ofile, "Module: %s, File: %s, Instance: %s\n", 
           root->mod->name, 
           root->mod->filename,
           root->name );
  fprintf( ofile, "--------------------------------------------------------\n" );

  combination_display_verbose( ofile, root->mod->stmt_head );

  curr_inst = root->child_head;
  while( curr_inst != NULL ) {
    combination_instance_verbose( ofile, curr_inst );
    curr_inst = curr_inst->next;
  }

}

/*!
 \param ofile  Pointer to file to output results to.
 \param head   Pointer to current module to evaluate.

 Outputs the verbose coverage report for the specified module
 to the specified output stream.
*/
void combination_module_verbose( FILE* ofile, mod_link* head ) {

  assert( head != NULL );

  fprintf( ofile, "\n" );
  fprintf( ofile, "Module: %s, File: %s\n", 
           head->mod->name, 
           head->mod->filename );
  fprintf( ofile, "--------------------------------------------------------\n" );

  combination_display_verbose( ofile, head->mod->stmt_head );
  
  if( head->next != NULL ) {
    combination_module_verbose( ofile, head->next );
  }

}

/*!
 \param ofile     Pointer to file to output results to.
 \param verbose   Specifies whether or not to provide verbose information
 \param instance  Specifies to report by instance or module.

 After the design is read into the module hierarchy, parses the hierarchy by module,
 reporting the combinational logic coverage for each module encountered.  The parent 
 module will specify its own combinational logic coverage along with a total combinational
 logic coverage including its children.
*/
void combination_report( FILE* ofile, bool verbose, bool instance ) {

  bool missed_found;      /* If set to TRUE, indicates combinations were missed */

  if( instance ) {

    fprintf( ofile, "COMBINATIONAL LOGIC COVERAGE RESULTS BY INSTANCE\n" );
    fprintf( ofile, "------------------------------------------------\n" );
    fprintf( ofile, "Instance                  Parent                       Logic Combinations\n" );
    fprintf( ofile, "                                                 Hit/Miss/Total    Percent hit\n" );
    fprintf( ofile, "------------------------------------------------------------------------------\n" );

    missed_found = combination_instance_summary( ofile, instance_root, "<root>" );
    
    if( verbose && missed_found ) {
      combination_instance_verbose( ofile, instance_root );
    }

  } else {

    fprintf( ofile, "COMBINATIONAL LOGIC COVERAGE RESULTS BY MODULE\n" );
    fprintf( ofile, "----------------------------------------------\n" );
    fprintf( ofile, "Module                    Filename                     Logical Combinations\n" );
    fprintf( ofile, "                                                 Hit/Miss/Total    Percent hit\n" );
    fprintf( ofile, "------------------------------------------------------------------------------\n" );

    missed_found = combination_module_summary( ofile, mod_head );

    if( verbose && missed_found ) {
      combination_module_verbose( ofile, mod_head );
    }

  }

}


/* $Log$
/* Revision 1.33  2002/07/14 05:27:34  phase1geo
/* Fixing report outputting to allow multiple modules/instances to be
/* output.
/*
/* Revision 1.32  2002/07/14 05:10:42  phase1geo
/* Added support for signal concatenation in score and report commands.  Fixed
/* bugs in this code (and multiplication).
/*
/* Revision 1.31  2002/07/10 16:27:17  phase1geo
/* Fixing output for single/multi-bit select signals in reports.
/*
/* Revision 1.30  2002/07/10 04:57:07  phase1geo
/* Adding bits to vector nibble to allow us to specify what type of input
/* static value was read in so that the output value may be displayed in
/* the same format (DECIMAL, BINARY, OCTAL, HEXIDECIMAL).  Full regression
/* passes.
/*
/* Revision 1.29  2002/07/10 03:01:50  phase1geo
/* Added define1.v and define2.v diagnostics to regression suite.  Both diagnostics
/* now pass.  Fixed cases where constants were not causing proper TRUE/FALSE values
/* to be calculated.
/*
/* Revision 1.28  2002/07/09 23:13:10  phase1geo
/* Fixing report output bug for conditionals.  Also adjusting combinational logic
/* report outputting.
/*
/* Revision 1.27  2002/07/09 17:27:25  phase1geo
/* Fixing default case item handling and in the middle of making fixes for
/* report outputting.
/*
/* Revision 1.26  2002/07/09 03:24:48  phase1geo
/* Various fixes for module instantiantion handling.  This now works.  Also
/* modified report output for toggle, line and combinational information.
/* Regression passes.
/*
/* Revision 1.25  2002/07/05 05:01:51  phase1geo
/* Removing unecessary debugging output.
/*
/* Revision 1.24  2002/07/05 05:00:13  phase1geo
/* Removing CASE, CASEX, and CASEZ from line and combinational logic results.
/*
/* Revision 1.23  2002/07/05 00:10:18  phase1geo
/* Adding report support for case statements.  Everything outputs fine; however,
/* I want to remove CASE, CASEX and CASEZ expressions from being reported since
/* it causes redundant and misleading information to be displayed in the verbose
/* reports.  New diagnostics to check CASE expressions have been added and pass.
/*
/* Revision 1.22  2002/07/03 21:30:52  phase1geo
/* Fixed remaining issues with always statements.  Full regression is running
/* error free at this point.  Regenerated documentation.  Added EOR expression
/* operation to handle the or expression in event lists.
/*
/* Revision 1.21  2002/07/03 19:54:36  phase1geo
/* Adding/fixing code to properly handle always blocks with the event control
/* structures attached.  Added several new diagnostics to test this ability.
/* always1.v is still failing but the rest are passing.
/*
/* Revision 1.20  2002/07/03 00:59:14  phase1geo
/* Fixing bug with conditional statements and other "deep" expression trees.
/*
/* Revision 1.19  2002/07/02 18:42:18  phase1geo
/* Various bug fixes.  Added support for multiple signals sharing the same VCD
/* symbol.  Changed conditional support to allow proper simulation results.
/* Updated VCD parser to allow for symbols containing only alphanumeric characters.
/*
/* Revision 1.18  2002/06/27 21:18:48  phase1geo
/* Fixing report Verilog output.  simple.v verilog diagnostic now passes.
/*
/* Revision 1.17  2002/06/27 20:39:43  phase1geo
/* Fixing scoring bugs as well as report bugs.  Things are starting to work
/* fairly well now.  Added rest of support for delays.
/*
/* Revision 1.16  2002/06/25 03:39:03  phase1geo
/* Fixed initial scoring bugs.  We now generate a legal CDD file for reporting.
/* Fixed some report bugs though there are still some remaining.
/*
/* Revision 1.15  2002/06/21 05:55:05  phase1geo
/* Getting some codes ready for writing simulation engine.  We should be set
/* now.
/*
/* Revision 1.14  2002/05/03 03:39:36  phase1geo
/* Removing all syntax errors due to addition of statements.  Added more statement
/* support code.  Still have a ways to go before we can try anything.  Removed lines
/* from expressions though we may want to consider putting these back for reporting
/* purposes.
/* */
