/*
 Copyright (c) 2006 Trevor Williams

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
 \file     instance.c
 \author   Trevor Williams  (phase1geo@gmail.com)
 \date     3/11/2002
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <stdlib.h>
#include <assert.h>

#include "arc.h"
#include "db.h"
#include "defines.h"
#include "expr.h"
#include "func_unit.h"
#include "gen_item.h"
#include "instance.h"
#include "link.h"
#include "param.h"
#include "static.h"
#include "util.h"


extern int          curr_expr_id;
extern db**         db_list;
extern unsigned int curr_db;
extern char         user_msg[USER_MSG_LENGTH];


/*!
 Signal ID that is used for identification purposes (each signal will receive a unique ID).
*/
int curr_sig_id = 1;


static bool instance_resolve_inst( funit_inst*, funit_inst* );
static void instance_dealloc_single( funit_inst* );


/*!
 Helper function for the \ref instance_display_tree function.
*/
static void instance_display_tree_helper(
  funit_inst* root,   /*!< Pointer to functional unit instance to display */
  char*       prefix  /*!< Prefix string to be used when outputting (used to indent children) */
) { PROFILE(INSTANCE_DISPLAY_TREE_HELPER);

  char         sp[4096];  /* Contains prefix for children */
  funit_inst*  curr;      /* Pointer to current child instance */
  unsigned int rv;        /* Return value from snprintf calls */

  assert( root != NULL );

  /* Get printable version of this instance and functional unit name */
  if( root->funit != NULL ) {
    char* piname = scope_gen_printable( root->name );
    char* pfname = scope_gen_printable( root->funit->name );
    printf( "%s%s (%s) - %p\n", prefix, piname, pfname, root );
    free_safe( piname, (strlen( piname ) + 1) );
    free_safe( pfname, (strlen( pfname ) + 1) );
  } else {
    char* piname = scope_gen_printable( root->name );
    printf( "%s%s () - %p\n", prefix, piname, root );
    free_safe( piname, (strlen( piname ) + 1) );
  }

  /* Calculate prefix */
  rv = snprintf( sp, 4096, "%s   ", prefix );
  assert( rv < 4096 );

  /* Display our children */
  curr = root->child_head;
  while( curr != NULL ) {
    instance_display_tree_helper( curr, sp );
    curr = curr->next;
  }


  PROFILE_END;

}

/*!
 Displays the given instance tree to standard output in a hierarchical format.  Shows
 instance names as well as associated module name.
*/
void instance_display_tree(
  funit_inst* root  /*!< Pointer to root instance to display */
) { PROFILE(INSTANCE_DISPLAY_TREE);

  instance_display_tree_helper( root, "" );

  PROFILE_END;

}

/*!
 \return Returns pointer to newly created functional unit instance.

 Creates a new functional unit instance from heap, initializes its data and
 returns a pointer to it.
*/
funit_inst* instance_create(
  func_unit*    funit,      /*!< Pointer to functional unit to store in this instance */
  char*         inst_name,  /*!< Instantiated name of this instance */
  bool          name_diff,  /*!< Specifies if the inst_name provided is not accurate due to merging */
  vector_width* range       /*!< For arrays of instances, contains range information for this array */
) { PROFILE(INSTANCE_CREATE);

  funit_inst* new_inst;  /* Pointer to new functional unit instance */

  new_inst             = (funit_inst*)malloc_safe( sizeof( funit_inst ) );
  new_inst->funit      = funit;
  new_inst->name       = strdup_safe( inst_name );
  new_inst->name_diff  = name_diff;
  new_inst->stat       = NULL;
  new_inst->param_head = NULL;
  new_inst->param_tail = NULL;
  new_inst->gitem_head = NULL;
  new_inst->gitem_tail = NULL;
  new_inst->parent     = NULL;
  new_inst->child_head = NULL;
  new_inst->child_tail = NULL;
  new_inst->next       = NULL;

  /* Create range (get a copy since this memory is managed by the parser) */
  if( range == NULL ) {
    new_inst->range = NULL;
  } else {
    assert( range->left  != NULL );
    assert( range->right != NULL );
    new_inst->range             = (vector_width*)malloc_safe( sizeof( vector_width ) );
    new_inst->range->left       = (static_expr*)malloc_safe( sizeof( static_expr ) );
    new_inst->range->left->num  = range->left->num;
    new_inst->range->left->exp  = range->left->exp;
    new_inst->range->right      = (static_expr*)malloc_safe( sizeof( static_expr ) );
    new_inst->range->right->num = range->right->num;
    new_inst->range->right->exp = range->right->exp;
  }

  PROFILE_END;

  return( new_inst );

}

/*!
 Recursively travels up to the root of the instance tree, building the scope
 string as it goes.  When the root instance is reached, the string is returned.
 Assumes that scope is initialized to the NULL character.
*/
void instance_gen_scope(
  char*       scope,   /*!< String pointer to store generated scope (assumed to be allocated) */
  funit_inst* leaf,    /*!< Pointer to leaf instance in scope */
  bool        flatten  /*!< Causes all unnamed scopes to be removed from generated scope if set to TRUE */
) { PROFILE(INSTANCE_GEN_SCOPE);

  if( leaf != NULL ) {

    /* Call parent instance first */
    instance_gen_scope( scope, leaf->parent, flatten );

    if( !flatten || !db_is_unnamed_scope( leaf->name ) ) {
      if( scope[0] != '\0' ) {
        strcat( scope, "." );
        strcat( scope, leaf->name );
      } else {
        strcpy( scope, leaf->name );
      }
    }

  }

  PROFILE_END;

}

/*!
 \return Returns TRUE if the given instance name and instance match.  If the specified instance is
         a part of an array of instances and the base name matches the base name of inst_name, we
         also check to make sure that the index of inst_name falls within the legal range of this
         instance.
*/
static bool instance_compare(
  char*             inst_name,  /*!< Instance name to compare to this instance's name (may contain array information) */
  const funit_inst* inst        /*!< Pointer to instance to compare name against */
) { PROFILE(INSTANCE_COMPARE);

  bool         retval = FALSE;  /* Return value of this function */
  char         bname[4096];     /* Base name of inst_name */
  int          index;           /* Index of inst_name */
  unsigned int width;           /* Width of instance range */
  int          lsb;             /* LSB of instance range */
  int          big_endian;      /* Specifies endianness */

  /* If this instance has a range, handle it */
  if( inst->range != NULL ) {

    /* Extract the index portion of inst_name if there is one */
    if( sscanf( inst_name, "%[a-zA-Z0-9_]\[%d]", bname, &index ) == 2 ) {
      
      /* If the base names compare, check that the given index falls within this instance range */
      if( scope_compare( bname, inst->name ) ) {

        /* Get range information from instance */
        static_expr_calc_lsb_and_width_post( inst->range->left, inst->range->right, &width, &lsb, &big_endian );
        assert( width != 0 );
        assert( lsb   != -1 );

        retval = (index >= lsb) && (index < (lsb + (int)width));

      }
      
    }

  } else {

    retval = scope_compare( inst_name, inst->name );

  }

  PROFILE_END;

  return( retval );

}

/*!
 \return Returns pointer to functional unit instance found by scope.
 
 Searches the specified functional unit instance tree for the specified
 scope.  When the functional unit instance is found, a pointer to that
 functional unit instance is passed back to the calling function.
*/
funit_inst* instance_find_scope(
  funit_inst* root,       /*!< Root of funit_inst tree to parse for scope */
  char*       scope,      /*!< Scope to search for */
  bool        rm_unnamed  /*!< Set to TRUE if we need to remove unnamed scopes */
) { PROFILE(INSTANCE_FIND_SCOPE);
 
  char        front[256];   /* Front of scope value */
  char        rest[4096];   /* Rest of scope value */
  funit_inst* inst = NULL;  /* Pointer to found instance */
  funit_inst* child;        /* Pointer to current child instance being traversed */

  assert( root != NULL );

  /* First extract the front scope */
  scope_extract_front( scope, front, rest );

  /* Skip this instance and move onto the children if we are an unnamed scope that does not contain signals */
  if( !rm_unnamed && db_is_unnamed_scope( root->name ) && !funit_is_unnamed( root->funit ) ) {
    child = root->child_head;
    while( (child != NULL) && ((inst = instance_find_scope( child, scope, rm_unnamed )) == NULL) ) {
      child = child->next;
    }

  /* Keep traversing if our name matches */
  } else if( instance_compare( front, root ) ) {
    if( rest[0] == '\0' ) {
      inst = root;
    } else {
      child = root->child_head;
      while( (child != NULL) && ((inst = instance_find_scope( child, rest, rm_unnamed )) == NULL) ) {
        child = child->next;
      }
    }
  }

  PROFILE_END;

  return( inst );

}

/*!
 \return Returns pointer to functional unit instance found by scope.
 
 Searches the specified functional unit instance tree for the specified
 functional unit.  When a functional unit instance is found that points to the specified
 functional unit and the ignore value is 0, a pointer to that functional unit instance is 
 passed back to the calling function; otherwise, the ignore count is
 decremented and the searching continues.
*/
funit_inst* instance_find_by_funit(
            funit_inst*      root,   /*!< Pointer to root functional unit instance of tree */
            const func_unit* funit,  /*!< Pointer to functional unit to find in tree */
  /*@out@*/ int*             ignore  /*!< Pointer to number of matches to ignore */
) { PROFILE(INSTANCE_FIND_BY_FUNIT);

  funit_inst* match_inst = NULL;  /* Pointer to functional unit instance that found a match */
  funit_inst* curr_child;         /* Pointer to current instances child functional unit instance */

  if( root != NULL ) {

    if( root->funit == funit ) {

      if( *ignore == 0 ) {
        match_inst = root;
      } else {
        (*ignore)--;
      }

    } else {

      curr_child = root->child_head;
      while( (curr_child != NULL) && (match_inst == NULL) ) {
        match_inst = instance_find_by_funit( curr_child, funit, ignore );
        curr_child = curr_child->next;
      }

    }
    
  }

  PROFILE_END;

  return( match_inst );

}

/*!
 Recursively searches the given instance tree, setting match_inst and matches if a matched functional unit name was found.
*/
static void instance_find_by_funit_name(
            funit_inst*   root,        /*!< Pointer to root functional unit instance to search */
            const char*   funit_name,  /*!< Name of module to find */
  /*@out@*/ funit_inst**  match_inst,  /*!< Pointer to matched functional unit instance */
  /*@out@*/ unsigned int* matches      /*!< Specifies the number of matched modules */
) { PROFILE(INSTANCE_FIND_BY_FUNIT_NAME_IF_ONE_HELPER);

  if( root != NULL ) {

    funit_inst* child;

    if( strcmp( root->funit->name, funit_name ) == 0 ) {
      (*matches)++;
      *match_inst = root;
    }

    child = root->child_head;
    while( child != NULL ) {
      instance_find_by_funit_name( child, funit_name, match_inst, matches );
      child = child->next;
    }

  }

  PROFILE_END;

}

/*!
 \return Returns a pointer to the found instance, if one exists; otherwise, returns NULL.
*/
static funit_inst* instance_find_by_funit_name_if_one(
  funit_inst* root,       /*!< Pointer to root functional unit instance to search */
  const char* funit_name  /*!< Name of module to find */
) { PROFILE(INSTANCE_FIND_BY_FUNIT_NAME_IF_ONE);

  funit_inst*  match_inst = NULL;
  unsigned int matches    = 0;

  instance_find_by_funit_name( root, funit_name, &match_inst, &matches );

  PROFILE_END;

  return( (matches == 1) ? match_inst : NULL );

}

/*!
 \return Returns the pointer to the signal that contains the same exclusion ID.

 Recursively searches the given instance tree to find the signal that has the same
 exclusion ID as the one specified.
*/
vsignal* instance_find_signal_by_exclusion_id(
            funit_inst* root,        /*!< Pointer to root instance */
            int         id,          /*!< Exclusion ID to search for */
  /*@out@*/ func_unit** found_funit  /*!< Pointer to functional unit containing this signal */
) { PROFILE(INSTANCE_FIND_SIGNAL_BY_EXCLUSION_ID);
 
  vsignal* sig = NULL;  /* Pointer to found signal */

  if( root != NULL ) {

    if( (root->funit != NULL) &&
        (root->funit->sig_head != NULL) &&
        (root->funit->sig_head->sig->id <= id) &&
        (root->funit->sig_tail->sig->id >= id) ) {

      sig_link* sigl = root->funit->sig_head;

      while( (sigl != NULL) && (sigl->sig->id != id) ) {
        sigl = sigl->next;
      }
      assert( sigl->sig != NULL );
      sig          = sigl->sig;
      *found_funit = root->funit;

    } else {

      funit_inst* child = root->child_head;
      while( (child != NULL) && ((sig = instance_find_signal_by_exclusion_id( child, id, found_funit )) == NULL) ) {
        child = child->next;
      }

    }
    
  }

  PROFILE_END;

  return( sig );

}

/*!
 \return Returns the pointer to the expression that contains the same exclusion ID. 
                                        
 Recursively searches the given instance tree to find the expression that has the same
 exclusion ID as the one specified.
*/
expression* instance_find_expression_by_exclusion_id(
            funit_inst* root,        /*!< Pointer to root instance */
            int         id,          /*!< Exclusion ID to search for */
  /*@out@*/ func_unit** found_funit  /*!< Pointer to functional unit containing this expression */
) { PROFILE(INSTANCE_FIND_EXPRESSION_BY_EXCLUSION_ID); 
    
  expression* exp = NULL;  /* Pointer to found expression */
    
  if( root != NULL ) {

    assert( root->funit != NULL );
 
    if( (root->funit->exp_head != NULL) && 
        (root->funit->exp_head->exp->id <= id) && 
        (root->funit->exp_tail->exp->id >= id) ) {

      exp_link* expl = root->funit->exp_head;

      while( (expl != NULL) && (expl->exp->id != id) ) {
        expl = expl->next;           
      }
      assert( expl->exp != NULL );
      exp          = expl->exp;
      *found_funit = root->funit;

    } else {

      funit_inst* child = root->child_head;
      while( (child != NULL) && ((exp = instance_find_expression_by_exclusion_id( child, id, found_funit )) == NULL) ) {
        child = child->next;
      }

    }
    
  }
  
  PROFILE_END; 
  
  return( exp );
  
}

/*!
 \return Returns the index of the state transition in the arcs array of the found_fsm in the found_funit that matches the
         given exclusion ID (if one is found); otherwise, returns -1.
*/
int instance_find_fsm_arc_index_by_exclusion_id(
            funit_inst* root,
            int         id,
  /*@out@*/ fsm_table** found_fsm,
  /*@out@*/ func_unit** found_funit
) { PROFILE(INSTANCE_FIND_FSM_ARC_INDEX_BY_EXCLUSION_ID);

  int arc_index = -1;  /* Index of found FSM arc */

  if( root != NULL ) {

    fsm_link* fsml;

    assert( root->funit != NULL );
  
    fsml = root->funit->fsm_head;
    while( (fsml != NULL) && ((arc_index = arc_find_arc_by_exclusion_id( fsml->table->table, id )) == -1) ) {
      fsml = fsml->next;
    }

    if( arc_index != -1 ) {
      *found_fsm   = fsml->table->table;
      *found_funit = root->funit;
    } else {
      funit_inst* child = root->child_head;
      while( (child != NULL) && ((arc_index = instance_find_fsm_arc_index_by_exclusion_id( child, id, found_fsm, found_funit )) == -1) ) {
        child = child->next;
      }
    }

  }

  PROFILE_END;

  return( arc_index );

}

/*!
 \return Returns pointer to newly created functional unit instance if this instance name isn't already in
         use in the current instance; otherwise, returns NULL.
 
 Generates new instance, adds it to the child list of the inst functional unit
 instance, and resolves any parameters.
*/
static funit_inst* instance_add_child(
  funit_inst*   inst,    /*!< Pointer to instance to add child instance to */
  func_unit*    child,   /*!< Pointer to child functional unit to create instance for */
  char*         name,    /*!< Name of instance to add */
  vector_width* range,   /*!< For arrays of instances, contains the range of the instance array */
  bool          resolve  /*!< Set to TRUE if newly added instance should be immediately resolved */
) { PROFILE(INSTANCE_ADD_CHILD);

  funit_inst* new_inst;  /* Pointer to newly created instance to add */

  /* Check to see if this instance already exists */
  new_inst = inst->child_head;
  while( (new_inst != NULL) && (strcmp( new_inst->name, name ) != 0) ) {
    new_inst = new_inst->next;
  }

  /* If this instance already exists, don't add it again */
  if( new_inst == NULL ) {

    /* Generate new instance */
    new_inst = instance_create( child, name, FALSE, range );

    /* Add new instance to inst child instance list */
    if( inst->child_head == NULL ) {
      inst->child_head       = new_inst;
      inst->child_tail       = new_inst;
    } else {
      inst->child_tail->next = new_inst;
      inst->child_tail       = new_inst;
    }

    /* Point this instance's parent pointer to its parent */
    new_inst->parent = inst;

    /* If the new instance needs to be resolved now, do so */
    if( resolve ) {
      inst_link* instl = db_list[curr_db]->inst_head;
      while( (instl != NULL) && !instance_resolve_inst( instl->inst, new_inst ) ) {
        instl = instl->next;
      }
    }

  } else {

    new_inst = NULL;

  }

  PROFILE_END;

  return( new_inst );

}

/*!
 Recursively copies the instance tree of from_inst to the instance
 to_inst, allocating memory for the new instances and resolving parameters.
*/
void instance_copy(
  funit_inst*   from_inst,  /*!< Pointer to instance tree to copy */
  funit_inst*   to_inst,    /*!< Pointer to instance to copy tree to */
  char*         name,       /*!< Instance name of current instance being copied */
  vector_width* range,      /*!< For arrays of instances, indicates the array range */
  bool          resolve     /*!< Set to TRUE if newly added instance should be immediately resolved */
) { PROFILE(INSTANCE_COPY);

  funit_inst* curr;      /* Pointer to current functional unit instance to copy */
  funit_inst* new_inst;  /* Pointer to newly created functional unit instance */

  assert( from_inst != NULL );
  assert( to_inst   != NULL );
  assert( name      != NULL );

  /* Add new child instance */
  new_inst = instance_add_child( to_inst, from_inst->funit, name, range, resolve );

  /* Do not add children if no child instance was created */
  if( new_inst != NULL ) {

    /* Iterate through rest of current child's list of children */
    curr = from_inst->child_head;
    while( curr != NULL ) {
      instance_copy( curr, new_inst, curr->name, curr->range, resolve );
      curr = curr->next;
    }

  }

  PROFILE_END;

}

/*!
 Searches given parent instance child list for a matching child to the specified
 child instance.  If the given child instance does not already exist in the parent's
 list of children, it is added and its parent pointer is pointed to the parent.

 \note
 This function creates a copy of the given child instance tree.
*/
void instance_attach_child(
  funit_inst* parent,  /*!< Pointer to parent instance to attach child to */
  funit_inst* child    /*!< Pointer to child instance tree to attach */
) { PROFILE(INSTANCE_ATTACH_CHILD);

  funit_inst* curr_inst;  /* Pointer to current instance */
  
  /* Check to see if this instance already exists */
  curr_inst = parent->child_head;
  while( (curr_inst != NULL) && (strcmp( curr_inst->name, child->name ) != 0) ) {
    curr_inst = curr_inst->next; 
  } 
  
  /* If this instance already exists, don't add it again */
  if( curr_inst == NULL ) {

    /* Create a copy of the given child instance */
    instance_copy( child, curr_inst, child->name, child->range, FALSE );
  
    /* Add new instance to inst child instance list */
    if( parent->child_head == NULL ) {
      parent->child_head       = curr_inst;
      parent->child_tail       = curr_inst;
    } else {
      parent->child_tail->next = curr_inst;
      parent->child_tail       = curr_inst;
    } 
    
    /* Point this instance's parent pointer to its parent */
    curr_inst->parent = parent; 

  }

  PROFILE_END;

}

/*!
 \return Returns TRUE if specified instance was successfully added to the specified instance tree;
         otherwise, returns FALSE.
 
 Adds the child functional unit to the child functional unit pointer list located in
 the functional unit specified by the scope of parent in the functional unit instance
 tree pointed to by root.  This function is used by the db_add_instance
 function during the parsing stage.
*/
bool instance_parse_add(
  funit_inst**  root,       /*!< Root funit_inst pointer of functional unit instance tree */
  func_unit*    parent,     /*!< Pointer to parent functional unit of specified child */
  func_unit*    child,      /*!< Pointer to child functional unit to add */
  char*         inst_name,  /*!< Name of new functional unit instance */
  vector_width* range,      /*!< For array of instances, specifies the name range */
  bool          resolve,    /*!< If set to TRUE, resolve any added instance */
  bool          child_gend  /*!< If set to TRUE, specifies that child is a generated instance and should only be added once */
) { PROFILE(INSTANCE_PARSE_ADD);
  
  bool        retval = TRUE;  /* Return value for this function */
  funit_inst* inst;           /* Temporary pointer to functional unit instance to add to */
  funit_inst* cinst;          /* Pointer to instance of child functional unit */
  int         i;              /* Loop iterator */
  int         ignore;         /* Number of matched instances to ignore */

  if( *root == NULL ) {

    *root = instance_create( child, inst_name, FALSE, range );

  } else {

    assert( parent != NULL );

    i      = 0;
    ignore = 0;

    /*
     Check to see if the child functional unit has already been parsed and, if so, find
     one of its instances for copying the instance tree below it.
    */
    cinst = instance_find_by_funit( *root, child, &ignore);
    
    /* Filename will be set to a value if the functional unit has been parsed */
    if( (cinst != NULL) && (cinst->funit->filename != NULL) ) { 

      ignore = 0;
      while( (ignore >= 0) && ((inst = instance_find_by_funit( *root, parent, &ignore )) != NULL) ) {
        instance_copy( cinst, inst, inst_name, range, resolve );
        i++;
        ignore = child_gend ? -1 : i;
      }

    } else {

      ignore = 0;
      while( (ignore >= 0) && ((inst = instance_find_by_funit( *root, parent, &ignore )) != NULL) ) {
        cinst = instance_add_child( inst, child, inst_name, range, resolve );
        i++;
        ignore = (child_gend && (cinst != NULL)) ? -1 : i;
      }

    }

    /* Everything went well with the add if we found at least one parent instance */
    retval = (i > 0);

  }

  PROFILE_END;

  return( retval );

}

/*!
 \return Returns TRUE if instance was resolved; otherwise, returns FALSE.

 Checks the given instance to see if a range was specified in its instantiation.  If
 a range was found, create all of the instances for this range and add them to the instance
 tree.
*/
bool instance_resolve_inst(
  funit_inst* root,  /*!< Pointer to root functional unit to traverse */
  funit_inst* curr   /*!< Pointer to current instance to resolve */
) { PROFILE(INSTANCE_RESOLVE_INST);

  unsigned int width = 0;   /* Width of the instance range */
  int          lsb;         /* LSB of the instance range */
  int          big_endian;  /* Unused */
  char*        name_copy;   /* Copy of the instance name being resolved */
  char*        new_name;    /* New hierarchical name of the instance(s) being resolved */
  unsigned int i;           /* Loop iterator */

  assert( curr != NULL );

  if( curr->range != NULL ) {

    unsigned int rv;
    unsigned int slen;

    /* Get LSB and width information */
    static_expr_calc_lsb_and_width_post( curr->range->left, curr->range->right, &width, &lsb, &big_endian );
    assert( width != 0 );
    assert( lsb != -1 );

    /* Remove the range information from this instance */
    static_expr_dealloc( curr->range->left,  FALSE );
    static_expr_dealloc( curr->range->right, FALSE );
    free_safe( curr->range, sizeof( vector_width ) );
    curr->range = NULL;

    /* Copy and deallocate instance name */
    name_copy = strdup_safe( curr->name );
    free_safe( curr->name, (strlen( curr->name ) + 1) );

    /* For the first instance, just modify the name */
    slen     = strlen( name_copy ) + 23;
    new_name = (char*)malloc_safe( slen );
    rv = snprintf( new_name, slen, "%s[%d]", name_copy, lsb );
    assert( rv < slen );
    curr->name = strdup_safe( new_name );

    /* For all of the rest of the instances, do the instance_parse_add function call */
    for( i=1; i<width; i++ ) {

      /* Create the new name */
      rv = snprintf( new_name, slen, "%s[%d]", name_copy, (lsb + i) );
      assert( rv < slen );

      /* Add the instance */
      (void)instance_parse_add( &root, ((curr->parent == NULL) ? NULL : curr->parent->funit), curr->funit, new_name, NULL, TRUE, FALSE );

    }

    /* Deallocate the new_name and name_copy pointers */
    free_safe( name_copy, (strlen( name_copy ) + 1) );
    free_safe( new_name, slen );

  }

  PROFILE_END;
  
  return( width != 0 );

}

/*!
 Recursively iterates through the entire instance tree
*/
static void instance_resolve_helper(
  funit_inst* root,  /*!< Pointer to root of instance tree */
  funit_inst* curr   /*!< Pointer to current instance */
) { PROFILE(INSTANCE_RESOLVE_HELPER);

  funit_inst* curr_child;  /* Pointer to current child */

  if( curr != NULL ) {

    /* Resolve all children first */
    curr_child = curr->child_head;
    while( curr_child != NULL ) {
      instance_resolve_helper( root, curr_child );
      curr_child = curr_child->next;
    }

    /* Now resolve this instance */
    (void)instance_resolve_inst( root, curr );

  }

  PROFILE_END;

}

/*!
 Recursively iterates through entire instance tree, resolving any instance arrays that are found.
*/
void instance_resolve(
  funit_inst* root  /*!< Pointer to current functional unit instance to resolve */
) { PROFILE(INSTANCE_RESOLVE);

  /* Resolve all instance names */
  instance_resolve_helper( root, root );

  PROFILE_END;

}

/*!
 \return Returns TRUE if instance was added to the specified functional unit instance tree; otherwise,
         returns FALSE (indicates that the instance is from a different hierarchy).

 Adds the child functional unit to the child functional unit pointer list located in
 the functional unit specified by the scope of parent in the functional unit instance
 tree pointed to by root.  This function is used by the db_read
 function during the CDD reading stage.
*/ 
bool instance_read_add(
  funit_inst** root,      /*!< Pointer to root instance of functional unit instance tree */
  char*        parent,    /*!< String scope of parent instance */
  func_unit*   child,     /*!< Pointer to child functional unit to add to specified parent's child list */
  char*        inst_name  /*!< Instance name of this child functional unit instance */
) { PROFILE(INSTANCE_READ_ADD);

  bool        retval = TRUE;  /* Return value for this function */
  funit_inst* inst;           /* Temporary pointer to functional unit instance to add to */
  funit_inst* new_inst;       /* Pointer to new functional unit instance to add */

  if( *root == NULL ) {

    *root = instance_create( child, inst_name, FALSE, NULL );

  } else {

    assert( parent != NULL );
  
    if( (inst = instance_find_scope( *root, parent, TRUE )) != NULL ) {

      /* Create new instance */
      new_inst = instance_create( child, inst_name, FALSE, NULL );

      if( inst->child_head == NULL ) {
        inst->child_head = new_inst;
        inst->child_tail = new_inst;
      } else {
        inst->child_tail->next = new_inst;
        inst->child_tail       = new_inst;
      }

      /* Set parent pointer of new instance */
      new_inst->parent = inst;

    } else {

      /* Unable to find parent of this child, needs to be added to a different instance tree */
      retval = FALSE;

    }
 
  }

  PROFILE_END;

  return( retval );

}

/*!
 Merges to instance trees that have the same instance root.
*/
static void instance_merge_tree(
  funit_inst* root1,  /*!< Pointer to root of first instance tree to merge */
  funit_inst* root2   /*!< Pointer to root of second instance tree to merge */
) { PROFILE(INSTANCE_MERGE);

  funit_inst* child2;
  funit_inst* last2 = NULL;

  /* Perform functional unit merging */
  if( root1->funit != NULL ) {
    if( root2->funit != NULL ) {
      funit_merge( root1->funit, root2->funit );
    }
  } else if( root2->funit != NULL ) {
    root1->funit = root2->funit;
    root2->funit = NULL;
  }

  /* Recursively merge the child instances */
  child2 = root2->child_head;
  while( child2 != NULL ) {
    funit_inst* child1 = root1->child_head;
    while( (child1 != NULL) && (strcmp( child1->name, child2->name ) != 0) ) {
      child1 = child1->next;
    }
    if( child1 != NULL ) {
      instance_merge_tree( child1, child2 );
      last2  = child2;
      child2 = child2->next;
    } else {
      funit_inst* tmp = child2->next;
      child2->next   = NULL;
      child2->parent = root1;
      if( root1->child_head == NULL ) {
        root1->child_head = child2;
        root1->child_tail = child2;
      } else {
        root1->child_tail->next = child2;
        root1->child_tail       = child2;
      }
      if( last2 == NULL ) {
        root2->child_head = tmp;
        if( tmp == NULL ) {
          root2->child_tail = NULL;
        }
      } else if( tmp == NULL ) {
        root2->child_tail = last2;
        last2->next = NULL;
      } else {
        last2->next = tmp;
      }
      child2 = tmp;
    }
  }

  PROFILE_END;

}

/*!
 Retrieves the leading hierarchy string and the pointer to the top-most populated instance
 given the specified instance tree.

 \note
 This function requires that the leading_hierarchy string be previously allocated and initialized
 to the NULL string.
*/
void instance_get_leading_hierarchy(
                 funit_inst*  root,               /*!< Pointer to instance tree to get information from */
  /*@out null@*/ char*        leading_hierarchy,  /*!< Leading hierarchy to first populated instance */
  /*@out@*/      funit_inst** top_inst            /*!< Pointer to first populated instance */
) { PROFILE(INSTANCE_GET_LEADING_HIERARCHY);

  if( leading_hierarchy != NULL ) {
    strcat( leading_hierarchy, root->name );
  }

  *top_inst = root;

  if( root->funit == NULL ) {

    do {
      root = root->child_head;
      if( leading_hierarchy != NULL ) {
        strcat( leading_hierarchy, "." );
        strcat( leading_hierarchy, root->name );
      }
      *top_inst = root;
    } while( (root != NULL) && (root->funit == NULL) );

  }

  PROFILE_END;

}

/*!
 Iterates up the scope for both functional unit
*/
static void instance_mark_lhier_diffs(
  funit_inst* root1,
  funit_inst* root2
) { PROFILE(INSTANCE_MARK_LHIER_DIFFS);

  /* Move up the scope hierarchy looking for a difference in instance names */
  while( (root1 != NULL) && (root2 != NULL) && (strcmp( root1->name, root2->name ) == 0) ) {
    root1 = root1->parent;
    root2 = root2->parent;
  }

  /*
   Iterate up root1 instance, setting the name_diff variable to TRUE to specify that the instance name is really
   not accurate since its child tree with a child tree with a differen parent scope.
  */
  while( root1 != NULL ) {
    root1->name_diff = TRUE;
    root1 = root1->parent;
  }

  PROFILE_END;

}

/*!
 \return Returns TRUE if the second instance tree should have its link removed from the
         instance tree list for the current database; otherwise, returns FALSE.

 Performs comples merges two instance trees into one instance tree.
*/
bool instance_merge_two_trees(
  funit_inst* root1,  /*!< Pointer to first instance tree to merge */
  funit_inst* root2   /*!< Pointer to second instance tree to merge */
) { PROFILE(INSTANCE_MERGE_TWO_TREES);

  bool        retval = TRUE;
  char        lhier1[4096];
  char        lhier2[4096];
  funit_inst* tinst1 = NULL;
  funit_inst* tinst2 = NULL;

  lhier1[0] = '\0';
  lhier2[0] = '\0';

  /* Get leading hierarchy information */
  instance_get_leading_hierarchy( root1, lhier1, &tinst1 );
  instance_get_leading_hierarchy( root2, lhier2, &tinst2 );

  /* If the top-level modules are the same, just merge them */
  if( (tinst1->funit != NULL) && (tinst2->funit != NULL) && (strcmp( tinst1->funit->name, tinst2->funit->name ) == 0) ) {

    if( strcmp( lhier1, lhier2 ) == 0 ) {

      instance_merge_tree( tinst1, tinst2 );

    } else {
      
      /* Create strings large enough to hold the contents from lhier1 and lhier2 */
      char* back1 = strdup_safe( lhier1 );
      char* rest1 = strdup_safe( lhier1 );
      char* back2 = strdup_safe( lhier2 );
      char* rest2 = strdup_safe( lhier2 );

      /* Break out the top-level instance name from the parent scope for each leading hierarchy string */
      scope_extract_back( lhier1, back1, rest1 );
      scope_extract_back( lhier2, back2, rest2 );

      /* If the leading hierarchies are different, just merge */
      if( strcmp( rest1, rest2 ) != 0 ) {
        instance_merge_tree( tinst1, tinst2 );
        instance_mark_lhier_diffs( tinst1, tinst2 );
      } else {
        instance_merge_tree( tinst1->parent, tinst2->parent );
      }

      /* Deallocate locally malloc'ed memory */
      free_safe( back1, (strlen( lhier1 ) + 1) );
      free_safe( rest1, (strlen( lhier1 ) + 1) );
      free_safe( back2, (strlen( lhier2 ) + 1) );
      free_safe( rest2, (strlen( lhier2 ) + 1) );

    }

  /* If root2 is a branch of root1, merge root2 into root1 */
  } else if( strncmp( lhier1, lhier2, strlen( lhier1 ) ) == 0 ) {

    root2 = instance_find_scope( root2, lhier1, FALSE );
    assert( root2 != NULL );
    instance_merge_tree( tinst1, root2 );

  /* If root1 is a branch of root2, merge root2 into root1 (replacing lower branches with those from root2) */
  } else if( strncmp( lhier1, lhier2, strlen( lhier2 ) ) == 0 ) {

    root1 = instance_find_scope( root1, lhier2, FALSE );
    assert( root1 != NULL );
    instance_merge_tree( root1, tinst2 );

  /* Check to see if the module pointed to by tinst1 exists within the tree of tinst2 */
  } else if( (root2 = instance_find_by_funit_name_if_one( tinst2, tinst1->funit->name )) != NULL ) {

    instance_merge_tree( tinst1, root2 );
    instance_mark_lhier_diffs( tinst1, root2 );

  /* Check to see if the module pointed to by tinst2 exists within the tree of tinst1 */
  } else if( (root1 = instance_find_by_funit_name_if_one( tinst1, tinst2->funit->name )) != NULL ) {

    instance_merge_tree( root1, tinst2 );
    instance_mark_lhier_diffs( root1, tinst2 );

  /* Otherwise, we cannot merge the two CDD files so don't */
  } else {

    retval = FALSE;

  }

  PROFILE_END;

  return( retval );

}

/*!
 \throws anonymous gen_item_assign_expr_ids instance_db_write funit_db_write

 Calls each functional unit display function in instance tree, starting with
 the root functional unit and ending when all of the leaf functional units are output.
 Note:  the function that calls this function originally should set
 the value of scope to NULL.
*/
void instance_db_write(
  funit_inst* root,        /*!< Root of functional unit instance tree to write */
  FILE*       file,        /*!< Output file to display contents to */
  char*       scope,       /*!< Scope of this functional unit */
  bool        parse_mode,  /*!< Specifies if we are parsing or scoring */
  bool        issue_ids,   /*!< Specifies that we need to issue expression and signal IDs */
  bool        report_save  /*!< Specifies if we are saving a CDD file after modifying it with the report command */
) { PROFILE(INSTANCE_DB_WRITE);

  bool stop_recursive = FALSE;

  assert( root != NULL );

  if( root->funit != NULL ) {

    if( root->funit->type != FUNIT_NO_SCORE ) {

      funit_inst* curr = parse_mode ? root : NULL;

      assert( scope != NULL );

      /* If we are in parse mode, re-issue expression IDs (we use the ulid field since it is not used in parse mode) */
      if( issue_ids && (root->funit != NULL) ) {

        exp_link*   expl;
        sig_link*   sigl;
#ifndef VPI_ONLY
        gitem_link* gil;
#endif

        /* First issue IDs to the expressions within the functional unit */
        expl = root->funit->exp_head;
        while( expl != NULL ) {
          expl->exp->ulid = curr_expr_id;
          curr_expr_id++;
          expl = expl->next;
        }

        sigl = root->funit->sig_head;
        while( sigl != NULL ) {
          sigl->sig->id = curr_sig_id;
          curr_sig_id++;
          sigl = sigl->next;
        }
    
#ifndef VPI_ONLY
        /* Then issue IDs to any generated expressions/signals */
        gil = root->gitem_head;
        while( gil != NULL ) {
          gen_item_assign_ids( gil->gi, root->funit );
          gil = gil->next;
        }
#endif

      }

      /* Display root functional unit */
      funit_db_write( root->funit, scope, root->name_diff, file, curr, report_save, issue_ids );

    } else {

      stop_recursive = TRUE;

    }

  } else {

    fprintf( file, "%d %s %d\n", DB_TYPE_INST_ONLY, scope, root->name_diff );

  }

  if( !stop_recursive ) {
 
    char tscope[4096];

    /* Display children */
    funit_inst* curr = root->child_head;
    while( curr != NULL ) {
      unsigned int rv = snprintf( tscope, 4096, "%s.%s", scope, curr->name );
      assert( rv < 4096 );
      instance_db_write( curr, file, tscope, parse_mode, issue_ids, report_save );
      curr = curr->next;
    }

  }

  PROFILE_END;

}

/*!
 Parses an instance-only database line and adds a "placeholder" instance in the instance tree.
*/
void instance_only_db_read(
  char** line  /*!< Pointer to line being read from database file */
) { PROFILE(INSTANCE_ONLY_DB_READ);

  char  scope[4096];
  int   chars_read;
  bool  name_diff;

  if( sscanf( *line, "%s %d%n", scope, &name_diff, &chars_read ) == 2 ) {

    char*       back = strdup_safe( scope );
    char*       rest = strdup_safe( scope );
    funit_inst* child;

    *line += chars_read;

    scope_extract_back( scope, back, rest ); 

    /* Create "placeholder" instance */
    child = instance_create( NULL, back, name_diff, NULL );

    /* If we are the top-most instance, just add ourselves to the instance link list */
    if( rest[0] == '\0' ) {
      inst_link_add( child, &(db_list[curr_db]->inst_head), &(db_list[curr_db]->inst_tail) );

    /* Otherwise, find our parent instance and attach the new instance to it */
    } else {
      funit_inst* parent;
      if( (parent = inst_link_find_by_scope( rest, db_list[curr_db]->inst_tail )) != NULL ) {
        if( parent->child_head == NULL ) {
          parent->child_head = parent->child_tail = child;
        } else {
          parent->child_tail->next = child;
          parent->child_tail       = child;
        }
        child->parent = parent;
      } else {
        print_output( "Unable to find parent instance of instance-only line in database file.", FATAL, __FILE__, __LINE__ );
        Throw 0;
      }
    }

    /* Deallocate memory */
    free_safe( back, (strlen( scope ) + 1) );
    free_safe( rest, (strlen( scope ) + 1) );

  } else {

    print_output( "Unable to read instance-only line in database file.", FATAL, __FILE__, __LINE__ );
    Throw 0;

  }

  PROFILE_END;

}

/*!
 Merges instance-only constructs from two CDD files.
*/
void instance_only_db_merge(
  char** line  /*!< Pointer to line being read from database file */
) { PROFILE(INSTANCE_ONLY_DB_MERGE);

  char scope[4096];
  int  chars_read;
  bool name_diff;

  if( sscanf( *line, "%s %d%n", scope, &name_diff, &chars_read ) == 2 ) {

    char*       back = strdup_safe( scope );
    char*       rest = strdup_safe( scope );
    funit_inst* child;

    *line += chars_read;

    scope_extract_back( scope, back, rest );

    /* Create "placeholder" instance */
    child = instance_create( NULL, back, name_diff, NULL );

    /* If we are the top-most instance, just add ourselves to the instance link list */
    if( rest[0] == '\0' ) {

      /* Add a new instance link if was not able to be found in the instance linked list */
      if( inst_link_find_by_scope( scope, db_list[curr_db]->inst_head ) == NULL ) {
        inst_link_add( child, &(db_list[curr_db]->inst_head), &(db_list[curr_db]->inst_tail) );
      }

    /* Otherwise, find our parent instance and attach the new instance to it */
    } else {
      funit_inst* parent;
      if( (parent = inst_link_find_by_scope( rest, db_list[curr_db]->inst_head )) != NULL ) {
        if( parent->child_head == NULL ) {
          parent->child_head = parent->child_tail = child;
        } else {
          parent->child_tail->next = child;
          parent->child_tail       = child;
        }
        child->parent = parent;
      } else {
        print_output( "Unable to find parent instance of instance-only line in database file.", FATAL, __FILE__, __LINE__ );
        Throw 0;
      }
    }

    /* Deallocate memory */
    free_safe( back, (strlen( scope ) + 1) );
    free_safe( rest, (strlen( scope ) + 1) );

  } else {

    print_output( "Unable to merge instance-only line in database file.", FATAL, __FILE__, __LINE__ );
    Throw 0;

  }

  PROFILE_END;

}

/*!
 Recursively iterates through instance tree, integrating all unnamed scopes that do
 not contain any signals into their parent modules.  This function only gets called
 during the report command.
*/
static void instance_flatten_helper(
  funit_inst*  root,     /*!< Pointer to current instance root */
  funit_link** rm_head,  /*!< Pointer to head of functional unit list to remove */
  funit_link** rm_tail   /*!< Pointer to head of functional unit list to remove */
) { PROFILE(INSTANCE_FLATTEN_HELPER);

  funit_inst* child;                 /* Pointer to current child instance */
  funit_inst* last_child    = NULL;  /* Pointer to the last child instance */
  funit_inst* tmp;                   /* Temporary pointer to functional unit instance */
  funit_inst* grandchild;            /* Pointer to current granchild instance */
  char        back[4096];            /* Last portion of functional unit name */
  char        rest[4096];            /* Holds the rest of the functional unit name */

  if( root != NULL ) {

    /* Iterate through child instances */
    child = root->child_head;
    while( child != NULL ) {

      /* First, flatten the child instance */
      instance_flatten_helper( child, rm_head, rm_tail );

      /* Get the last portion of the child instance before this functional unit is removed */
      scope_extract_back( child->funit->name, back, rest );

      /*
       Next, fold this child instance into this instance if it is an unnamed scope
       that has no signals.
      */
      if( funit_is_unnamed( child->funit ) && (child->funit->sig_head == NULL) ) {

        /* Remove this child from the child list of this instance */
        if( child == root->child_head ) {
          if( child == root->child_tail ) {
            root->child_head = root->child_tail = NULL;
          } else {
            root->child_head = child->next;
          }
        } else {
          if( child == root->child_tail ) {
            root->child_tail = last_child;
            root->child_tail->next = NULL;
          } else {
            last_child->next = child->next;
          }
        }

        /* Add grandchildren to this parent */
        grandchild = child->child_head;
        if( grandchild != NULL ) {
          while( grandchild != NULL ) {
            grandchild->parent = root;
            grandchild = grandchild->next;
          }
          if( root->child_head == NULL ) {
            root->child_head = root->child_tail = child->child_head;
          } else {
            root->child_tail->next = child->child_head;
            root->child_tail       = child->child_head;
          }
        }

        tmp   = child;
        child = child->next;

        /* Add the current functional unit to the list of functional units to remove */
        if( funit_link_find( tmp->funit->name, tmp->funit->type, *rm_head ) == NULL ) {
          funit_link_add( tmp->funit, rm_head, rm_tail );
        }

        /* Deallocate child instance */
        instance_dealloc_single( tmp );
      
      } else {

        last_child = child;
        child = child->next;

      }

    }

  }

  PROFILE_END;

}

/*!
 Recursively iterates through instance tree, integrating all unnamed scopes that do
 not contain any signals into their parent modules.  This function only gets called
 during the report command.
*/
void instance_flatten(
  funit_inst* root  /*!< Pointer to current instance root */
) { PROFILE(INSTANCE_FLATTEN);

  funit_link* rm_head = NULL;  /* Pointer to head of functional unit list to remove */
  funit_link* rm_tail = NULL;  /* Pointer to tail of functional unit list to remove */
  func_unit*  parent_mod;      /* Pointer to parent module of list to remove */
  funit_link* funitl;          /* Pointer to current functional unit link */

  /* Flatten the hierarchy */
  instance_flatten_helper( root, &rm_head, &rm_tail );

  /* Now deallocate the list of functional units */
  funitl = rm_head;
  while( funitl != NULL ) {
    funit_link_remove( funitl->funit, &(db_list[curr_db]->funit_head), &(db_list[curr_db]->funit_tail), FALSE );
    if( funitl->funit->type != FUNIT_MODULE ) {
      parent_mod = funit_get_curr_module( funitl->funit );
      funit_link_remove( funitl->funit, &(parent_mod->tf_head), &(parent_mod->tf_tail), FALSE );
    }
    funitl = funitl->next;
  }
  funit_link_delete_list( &rm_head, &rm_tail, TRUE );

  PROFILE_END;

}

/*!
 Removes all statement blocks in the design that call that specified statement.
*/
void instance_remove_stmt_blks_calling_stmt(
  funit_inst* root,  /*!< Pointer to root instance to remove statements from */
  statement*  stmt   /*!< Pointer to statement to match */
) { PROFILE(INSTANCE_REMOVE_STMT_BLKS_CALLING_STMT);

  funit_inst* curr_child;  /* Pointer to current child instance to parse */
#ifndef VPI_ONLY
  gitem_link* gil;         /* Pointer to current generate item link */
#endif

  if( root != NULL ) {

    /* First, handle the current functional unit */
    funit_remove_stmt_blks_calling_stmt( root->funit, stmt );

#ifndef VPI_ONLY
    /* Second, handle all generate items in this instance */
    gil = root->gitem_head;
    while( gil != NULL ) {
      gen_item_remove_if_contains_expr_calling_stmt( gil->gi, stmt );
      gil = gil->next;
    }
#endif

    /* Parse children */
    curr_child = root->child_head;
    while( curr_child != NULL ) {
      instance_remove_stmt_blks_calling_stmt( curr_child, stmt );
      curr_child = curr_child->next;
    }

  }

  PROFILE_END;

}

/*!
 Recursively traverses the given instance tree, removing the given statement.
*/
void instance_remove_parms_with_expr(
  funit_inst* root,  /*!< Pointer to functional unit instance to remove expression from */
  statement*  stmt   /*!< Pointer to statement to remove from list */
) { PROFILE(INSTANCE_REMOVE_PARMS_WITH_EXPR);

  funit_inst* curr_child;  /* Pointer to current child instance to traverse */
  inst_parm*  iparm;       /* Pointer to current instance parameter */
  exp_link*   expl;        /* Pointer to current expression link */
  exp_link*   texpl;       /* Temporary pointer to current expression link */

  /* Search for the given expression within the given instance parameter */
  iparm = root->param_head;
  while( iparm != NULL ) {
    if( iparm->sig != NULL ) {
      expl = iparm->sig->exp_head;
      while( expl != NULL ) {
        texpl = expl;
        expl  = expl->next;
        if( expression_find_expr( stmt->exp, texpl->exp ) ) {
          if( iparm->mparm != NULL ) {
            exp_link_remove( texpl->exp, &(iparm->mparm->exp_head), &(iparm->mparm->exp_tail), FALSE );
          }
          exp_link_remove( texpl->exp, &(iparm->sig->exp_head), &(iparm->sig->exp_tail), FALSE );
        }
      }
    }
    iparm = iparm->next;
  }

  /* Traverse children */
  curr_child = root->child_head;
  while( curr_child != NULL ) {
    instance_remove_parms_with_expr( curr_child, stmt );
    curr_child = curr_child->next;
  }

  PROFILE_END;

}

/*!
 Deallocates all memory allocated for the given instance.
*/
void instance_dealloc_single(
  funit_inst* inst  /*!< Pointer to instance to deallocate memory for */
) { PROFILE(INSTANCE_DEALLOC_SINGLE);

  if( inst != NULL ) {

    /* Free up memory allocated for name */
    free_safe( inst->name, (strlen( inst->name ) + 1) );

    /* Free up memory allocated for statistic, if necessary */
    free_safe( inst->stat, sizeof( statistic ) );

    /* Free up memory for range, if necessary */
    if( inst->range != NULL ) {
      static_expr_dealloc( inst->range->left,  FALSE );
      static_expr_dealloc( inst->range->right, FALSE );
      free_safe( inst->range, sizeof( vector_width ) );
    }

    /* Deallocate memory for instance parameter list */
    inst_parm_dealloc( inst->param_head, TRUE );

#ifndef VPI_ONLY
    /* Deallocate memory for generate item list */
    gitem_link_delete_list( inst->gitem_head, FALSE );
#endif

    /* Free up memory for this functional unit instance */
    free_safe( inst, sizeof( funit_inst ) );

  }

  PROFILE_END;

}

/*!
 Outputs dumpvars to the specified file.
*/
void instance_output_dumpvars(
  FILE*       vfile,  /*!< Pointer to file to output dumpvars to */
  funit_inst* root    /*!< Pointer to current instance */
) { PROFILE(INSTANCE_OUTPUT_DUMPVARS);

  funit_inst* child = root->child_head;
  char        scope[4096];

  /* Generate instance scope */
  scope[0] = '\0';
  instance_gen_scope( scope, root, FALSE );

  /* Outputs dumpvars for the given functional unit */
  funit_output_dumpvars( vfile, root->funit, scope );

  /* Outputs all children instances */
  while( child != NULL ) {
    instance_output_dumpvars( vfile, child );
    child = child->next;
  }

  PROFILE_END;

}

/*!
 Recursively traverses instance tree, deallocating heap memory used to store the
 the tree.
*/
void instance_dealloc_tree(
  funit_inst* root  /*!< Pointer to root instance of functional unit instance tree to remove */
) { PROFILE(INSTANCE_DEALLOC_TREE);

  funit_inst* curr;  /* Pointer to current instance to evaluate */
  funit_inst* tmp;   /* Temporary pointer to instance */

  if( root != NULL ) {

    /* Remove instance's children first */
    curr = root->child_head;
    while( curr != NULL ) {
      tmp = curr->next;
      instance_dealloc_tree( curr );
      curr = tmp;
    }

    /* Deallocate the instance memory */
    instance_dealloc_single( root );

  }

  PROFILE_END;

}

/*!
 Searches tree for specified functional unit.  If the functional unit instance is found,
 the functional unit instance is removed from the tree along with all of its
 child functional unit instances.
*/
void instance_dealloc(
  funit_inst* root,  /*!< Root of functional unit instance tree */
  char*       scope  /*!< Scope of functional unit to remove from tree */
) { PROFILE(INSTANCE_DEALLOC);
  
  funit_inst* inst;        /* Pointer to instance to remove */
  funit_inst* curr;        /* Pointer to current child instance to remove */
  funit_inst* last;        /* Last current child instance */
  char        back[256];   /* Highest level of hierarchy in hierarchical reference */
  char        rest[4096];  /* Rest of scope value */
  
  assert( root  != NULL );
  assert( scope != NULL );
  
  if( scope_compare( root->name, scope ) ) {
    
    /* We are the root so just remove the whole tree */
    instance_dealloc_tree( root );
    
  } else {
    
    /* 
     Find parent instance of given scope and remove this instance
     from its child list.
    */  
    scope_extract_back( scope, back, rest );
    assert( rest[0] != '\0' );

    inst = instance_find_scope( root, rest, TRUE );
    assert( inst != NULL );

    curr = inst->child_head;
    last = NULL;
    while( (curr != NULL) && !scope_compare( curr->name, scope ) ) {
      last = curr;
      curr = curr->next;
    }

    if( curr != NULL ) {
      if( last != NULL ) {
        last->next = curr->next;
      }
      if( curr == inst->child_head ) {
        /* Move parent head pointer */
        inst->child_head = curr->next;
      }
      if( curr == inst->child_tail ) {
        /* Move parent tail pointer */
        inst->child_tail = last;
      }
    }

    instance_dealloc_tree( curr );

  }

  PROFILE_END;

}

/*
 $Log$
 Revision 1.116  2008/11/13 22:42:15  phase1geo
 Adding new merge9 diagnostic which merges to non-overlapping trees that are
 generated via an instance array.  Added code to instance.c to fix merging hole
 that was made visible with this diagnostic.  Full regressions pass.

 Revision 1.115  2008/11/13 05:08:36  phase1geo
 Fixing bug found with merge8.5 diagnostic and fixing issues with VPI.  Full
 regressions now pass.

 Revision 1.114  2008/11/12 19:57:07  phase1geo
 Fixing the rest of the issues from regressions in regards to the merge changes.
 Updating regression files.  IV and Cver regressions now pass.

 Revision 1.113  2008/11/12 07:04:01  phase1geo
 Fixing argument merging and updating regressions.  Checkpointing.

 Revision 1.112  2008/11/12 00:07:41  phase1geo
 More updates for complex merging algorithm.  Updating regressions per
 these changes.  Checkpointing.

 Revision 1.111  2008/11/11 14:28:49  phase1geo
 Checkpointing.

 Revision 1.110  2008/11/11 05:36:40  phase1geo
 Checkpointing merge code.

 Revision 1.109  2008/11/11 00:10:19  phase1geo
 Starting to work on instance tree merging algorithm (not complete at this point).
 Checkpointing.

 Revision 1.108  2008/11/08 00:09:04  phase1geo
 Checkpointing work on asymmetric merging algorithm.  Updated regressions
 per these changes.  We currently have 5 failures in the IV regression suite.

 Revision 1.107  2008/10/31 22:01:34  phase1geo
 Initial code changes to support merging two non-overlapping CDD files into
 one.  This functionality seems to be working but needs regression testing to
 verify that nothing is broken as a result.

 Revision 1.106  2008/10/07 05:24:17  phase1geo
 Adding -dumpvars option.  Need to resolve a few issues before this work is considered
 complete.

 Revision 1.105  2008/09/24 22:47:28  phase1geo
 Fixing bugs 2125451 and 2127185.  Adding if1 diagnostic to verify the fix for
 2127185.

 Revision 1.104  2008/09/03 05:33:06  phase1geo
 Adding in FSM exclusion support to exclude and report -e commands.  Updating
 regressions per recent changes.  Checkpointing.

 Revision 1.103  2008/09/02 22:41:45  phase1geo
 Starting to work on adding exclusion reason output to report files.  Added
 support for exclusion reasons to CDD files.  Checkpointing.

 Revision 1.102  2008/09/02 05:53:54  phase1geo
 More code additions for exclude command.  Fixing a few bugs in this code as well.
 Checkpointing.

 Revision 1.101  2008/09/02 05:20:41  phase1geo
 More updates for exclude command.  Updates to CVER regression.

 Revision 1.100  2008/08/28 04:37:18  phase1geo
 Starting to add support for exclusion output and exclusion IDs to generated
 reports.  These changes should break regressions.  Checkpointing.

 Revision 1.99  2008/08/27 23:06:00  phase1geo
 Starting to make updates for supporting command-line exclusions.  Signals now
 have a unique ID associated with them in the CDD file.  Checkpointing.

 Revision 1.98  2008/08/18 23:07:28  phase1geo
 Integrating changes from development release branch to main development trunk.
 Regression passes.  Still need to update documentation directories and verify
 that the GUI stuff works properly.

 Revision 1.95.4.2  2008/08/06 20:11:34  phase1geo
 Adding support for instance-based coverage reporting in GUI.  Everything seems to be
 working except for proper exclusion handling.  Checkpointing.

 Revision 1.95.4.1  2008/07/10 22:43:52  phase1geo
 Merging in rank-devel-branch into this branch.  Added -f options for all commands
 to allow files containing command-line arguments to be added.  A few error diagnostics
 are currently failing due to changes in the rank branch that never got fixed in that
 branch.  Checkpointing.

 Revision 1.96  2008/06/27 14:02:02  phase1geo
 Fixing splint and -Wextra warnings.  Also fixing comment formatting.

 Revision 1.95  2008/04/15 20:37:11  phase1geo
 Fixing database array support.  Full regression passes.

 Revision 1.94  2008/04/15 06:08:47  phase1geo
 First attempt to get both instance and module coverage calculatable for
 GUI purposes.  This is not quite complete at the moment though it does
 compile.

 Revision 1.93  2008/03/18 21:36:24  phase1geo
 Updates from regression runs.  Regressions still do not completely pass at
 this point.  Checkpointing.

 Revision 1.92  2008/03/17 22:02:31  phase1geo
 Adding new check_mem script and adding output to perform memory checking during
 regression runs.  Completed work on free_safe and added realloc_safe function
 calls.  Regressions are pretty broke at the moment.  Checkpointing.

 Revision 1.91  2008/03/17 05:26:16  phase1geo
 Checkpointing.  Things don't compile at the moment.

 Revision 1.90  2008/03/11 22:06:48  phase1geo
 Finishing first round of exception handling code.

 Revision 1.89  2008/03/04 00:09:20  phase1geo
 More exception handling.  Checkpointing.

 Revision 1.88  2008/01/30 05:51:50  phase1geo
 Fixing doxygen errors.  Updated parameter list syntax to make it more readable.

 Revision 1.87  2008/01/16 06:40:37  phase1geo
 More splint updates.

 Revision 1.86  2008/01/10 04:59:04  phase1geo
 More splint updates.  All exportlocal cases are now taken care of.

 Revision 1.85  2008/01/08 21:13:08  phase1geo
 Completed -weak splint run.  Full regressions pass.

 Revision 1.84  2008/01/07 23:59:54  phase1geo
 More splint updates.

 Revision 1.83  2007/12/18 23:55:21  phase1geo
 Starting to remove 64-bit time and replacing it with a sim_time structure
 for performance enhancement purposes.  Also removing global variables for time-related
 information and passing this information around by reference for performance
 enhancement purposes.

 Revision 1.82  2007/12/11 05:48:25  phase1geo
 Fixing more compile errors with new code changes and adding more profiling.
 Still have a ways to go before we can compile cleanly again (next submission
 should do it).

 Revision 1.81  2007/11/20 05:28:58  phase1geo
 Updating e-mail address from trevorw@charter.net to phase1geo@gmail.com.

 Revision 1.80  2007/09/13 17:03:30  phase1geo
 Cleaning up some const-ness corrections -- still more to go but it's a good
 start.

 Revision 1.79  2007/08/31 22:46:36  phase1geo
 Adding diagnostics from stable branch.  Fixing a few minor bugs and in progress
 of working on static_afunc1 failure (still not quite there yet).  Checkpointing.

 Revision 1.78  2007/07/18 22:39:17  phase1geo
 Checkpointing generate work though we are at a fairly broken state at the moment.

 Revision 1.77  2007/07/18 02:15:04  phase1geo
 Attempts to fix a problem with generating instances with hierarchy.  Also fixing
 an issue with named blocks in generate statements.  Still some work to go before
 regressions are passing again, however.

 Revision 1.76  2007/04/18 22:35:02  phase1geo
 Revamping simulator core again.  Checkpointing.

 Revision 1.75  2007/04/11 22:29:48  phase1geo
 Adding support for CLI to score command.  Still some work to go to get history
 stuff right.  Otherwise, it seems to be working.

 Revision 1.74  2007/04/09 22:47:53  phase1geo
 Starting to modify the simulation engine for performance purposes.  Code is
 not complete and is untested at this point.

 Revision 1.73  2007/04/03 04:15:17  phase1geo
 Fixing bugs in func_iter functionality.  Modified functional unit name
 flattening function (though this does not appear to be working correctly
 at this time).  Added calls to funit_flatten_name in all of the reporting
 files.  Checkpointing.

 Revision 1.72  2007/04/02 20:19:36  phase1geo
 Checkpointing more work on use of functional iterators.  Not working correctly
 yet.

 Revision 1.71  2007/03/19 20:30:31  phase1geo
 More fixes to report command for instance flattening.  This seems to be
 working now as far as I can tell.  Regressions still have about 8 diagnostics
 failing with report errors.  Checkpointing.

 Revision 1.70  2007/03/19 03:30:16  phase1geo
 More fixes to instance flattening algorithm.  Still much more work to do here.
 Checkpointing.

 Revision 1.69  2007/03/16 21:41:09  phase1geo
 Checkpointing some work in fixing regressions for unnamed scope additions.
 Getting closer but still need to properly handle the removal of functional units.

 Revision 1.68  2007/03/15 22:39:05  phase1geo
 Fixing bug in unnamed scope binding.

 Revision 1.67  2006/12/19 06:06:05  phase1geo
 Shortening unnamed scope name from $unnamed_%d to $u%d.  Also fixed a few
 bugs in the instance_flatten function (still more debug work to go here).
 Checkpointing.

 Revision 1.66  2006/12/19 05:23:39  phase1geo
 Added initial code for handling instance flattening for unnamed scopes.  This
 is partially working at this point but still needs some debugging.  Checkpointing.

 Revision 1.65  2006/10/16 21:34:46  phase1geo
 Increased max bit width from 1024 to 65536 to allow for more room for memories.
 Fixed issue with enumerated values being explicitly assigned unknown values and
 created error output message when an implicitly assigned enum followed an explicitly
 assign unknown enum value.  Fixed problem with generate blocks in different
 instantiations of the same module.  Fixed bug in parser related to setting the
 curr_packed global variable correctly.  Added enum2 and enum2.1 diagnostics to test
 suite to verify correct enumerate behavior for the changes made in this checkin.
 Full regression now passes.

 Revision 1.64  2006/10/13 22:46:31  phase1geo
 Things are a bit of a mess at this point.  Adding generate12 diagnostic that
 shows a failure in properly handling generates of instances.

 Revision 1.63  2006/10/12 22:48:46  phase1geo
 Updates to remove compiler warnings.  Still some work left to go here.

 Revision 1.62  2006/10/09 17:54:19  phase1geo
 Fixing support for VPI to allow it to properly get linked to the simulator.
 Also fixed inconsistency in generate reports and updated appropriately in
 regressions for this change.  Full regression now passes.

 Revision 1.61  2006/09/22 19:56:45  phase1geo
 Final set of fixes and regression updates per recent changes.  Full regression
 now passes.

 Revision 1.60  2006/09/22 04:23:04  phase1geo
 More fixes to support new signal range structure.  Still don't have full
 regressions passing at the moment.

 Revision 1.59  2006/09/08 22:39:50  phase1geo
 Fixes for memory problems.

 Revision 1.58  2006/09/07 21:59:24  phase1geo
 Fixing some bugs related to statement block removal.  Also made some significant
 optimizations to this code.

 Revision 1.57  2006/09/01 23:06:02  phase1geo
 Fixing regressions per latest round of changes.  Full regression now passes.

 Revision 1.56  2006/09/01 04:06:37  phase1geo
 Added code to support more than one instance tree.  Currently, I am seeing
 quite a few memory errors that are causing some major problems at the moment.
 Checkpointing.

 Revision 1.55  2006/07/27 16:08:46  phase1geo
 Fixing several memory leak bugs, cleaning up output and fixing regression
 bugs.  Full regression now passes (including all current generate diagnostics).

 Revision 1.54  2006/07/21 22:39:01  phase1geo
 Started adding support for generated statements.  Still looks like I have
 some loose ends to tie here before I can call it good.  Added generate5
 diagnostic to regression suite -- this does not quite pass at this point, however.

 Revision 1.53  2006/07/21 20:12:46  phase1geo
 Fixing code to get generated instances and generated array of instances to
 work.  Added diagnostics to verify correct functionality.  Full regression
 passes.

 Revision 1.52  2006/07/18 19:03:21  phase1geo
 Sync'ing up to the scoping fixes from the 0.4.6 stable release.

 Revision 1.51  2006/07/17 22:12:42  phase1geo
 Adding more code for generate block support.  Still just adding code at this
 point -- hopefully I haven't broke anything that doesn't use generate blocks.

 Revision 1.50  2006/07/12 22:16:18  phase1geo
 Fixing hierarchical referencing for instance arrays.  Also attempted to fix
 a problem found with unary1; however, the generated report coverage information
 does not look correct at this time.  Checkpointing what I have done for now.

 Revision 1.49  2006/07/11 04:59:08  phase1geo
 Reworking the way that instances are being generated.  This is to fix a bug and
 pave the way for generate loops for instances.  Code not working at this point
 and may cause serious problems for regression runs.

 Revision 1.48  2006/07/10 19:30:55  phase1geo
 Fixing bug in instance.c that ignored the LSB information for an instance
 array (this also needs to be fixed for the 0.4.6 stable release).  Added
 diagnostic to verify correctness of this behavior.  Also added case statement
 to the generate parser.

 Revision 1.47  2006/07/10 03:05:04  phase1geo
 Contains bug fixes for memory leaks and segmentation faults.  Also contains
 some starting code to support generate blocks.  There is absolutely no
 functionality here, however.

 Revision 1.46  2006/06/27 19:34:43  phase1geo
 Permanent fix for the CDD save feature.

 Revision 1.45  2006/05/28 02:43:49  phase1geo
 Integrating stable release 0.4.4 changes into main branch.  Updated regressions
 appropriately.

 Revision 1.44  2006/05/25 12:11:01  phase1geo
 Including bug fix from 0.4.4 stable release and updating regressions.

 Revision 1.43  2006/04/21 06:14:45  phase1geo
 Merged in changes from 0.4.3 stable release.  Updated all regression files
 for inclusion of OVL library.  More documentation updates for next development
 release (but there is more to go here).

 Revision 1.42  2006/04/08 03:23:28  phase1geo
 Adding support for CVER simulator VPI support.  I think I may have also fixed
 support for VCS also.  Recreated configuration/Makefiles with newer version of
 auto* tools.

 Revision 1.41  2006/04/07 22:31:07  phase1geo
 Fixes to get VPI to work with VCS.  Getting close but still some work to go to
 get the callbacks to start working.

 Revision 1.40.4.1  2006/04/20 21:55:16  phase1geo
 Adding support for big endian signals.  Added new endian1 diagnostic to regression
 suite to verify this new functionality.  Full regression passes.  We may want to do
 some more testing on variants of this before calling it ready for stable release 0.4.3.

 Revision 1.40  2006/03/28 22:28:27  phase1geo
 Updates to user guide and added copyright information to each source file in the
 src directory.  Added test directory in user documentation directory containing the
 example used in line, toggle, combinational logic and FSM descriptions.

 Revision 1.39  2006/02/17 19:50:47  phase1geo
 Added full support for escaped names.  Full regression passes.

 Revision 1.38  2006/02/16 21:19:26  phase1geo
 Adding support for arrays of instances.  Also fixing some memory problems for
 constant functions and fixed binding problems when hierarchical references are
 made to merged modules.  Full regression now passes.

 Revision 1.37  2006/01/24 23:24:38  phase1geo
 More updates to handle static functions properly.  I have redone quite a bit
 of code here which has regressions pretty broke at the moment.  More work
 to do but I'm checkpointing.

 Revision 1.36  2006/01/20 22:50:50  phase1geo
 Code cleanup.

 Revision 1.35  2006/01/20 22:44:51  phase1geo
 Moving parameter resolution to post-bind stage to allow static functions to
 be considered.  Regression passes without static function testing.  Static
 function support still has some work to go.  Checkpointing.

 Revision 1.34  2006/01/20 19:15:23  phase1geo
 Fixed bug to properly handle the scoping of parameters when parameters are created/used
 in non-module functional units.  Added param10*.v diagnostics to regression suite to
 verify the behavior is correct now.

 Revision 1.33  2006/01/16 17:27:41  phase1geo
 Fixing binding issues when designs have modules/tasks/functions that are either used
 more than once in a design or have the same name.  Full regression now passes.

 Revision 1.32  2005/12/01 16:08:19  phase1geo
 Allowing nested functional units within a module to get parsed and handled correctly.
 Added new nested_block1 diagnostic to test nested named blocks -- will add more tests
 later for different combinations.  Updated regression suite which now passes.

 Revision 1.31  2005/11/08 23:12:09  phase1geo
 Fixes for function/task additions.  Still a lot of testing on these structures;
 however, regressions now pass again so we are checkpointing here.

 Revision 1.30  2004/03/16 05:45:43  phase1geo
 Checkin contains a plethora of changes, bug fixes, enhancements...
 Some of which include:  new diagnostics to verify bug fixes found in field,
 test generator script for creating new diagnostics, enhancing error reporting
 output to include filename and line number of failing code (useful for error
 regression testing), support for error regression testing, bug fixes for
 segmentation fault errors found in field, additional data integrity features,
 and code support for GUI tool (this submission does not include TCL files).

 Revision 1.29  2004/03/15 21:38:17  phase1geo
 Updated source files after running lint on these files.  Full regression
 still passes at this point.

 Revision 1.28  2003/01/14 05:52:16  phase1geo
 Fixing bug related to copying instance trees in modules that were previously
 parsed.  Added diagnostic param7.v to testsuite and regression.  Full
 regression passes.

 Revision 1.27  2003/01/13 14:30:05  phase1geo
 Initial code to fix problem with missing instances in CDD files.  Instance
 now shows up but parameters not calculated correctly.  Another checkin to
 follow will contain full fix.

 Revision 1.26  2003/01/04 03:56:27  phase1geo
 Fixing bug with parameterized modules.  Updated regression suite for changes.

 Revision 1.25  2003/01/03 05:53:19  phase1geo
 Removing unnecessary spaces.

 Revision 1.24  2002/12/05 14:45:17  phase1geo
 Removing assertion error from instance6.1 failure; however, this case does not
 work correctly according to instance6.2.v diagnostic.  Added @(...) output in
 report command for edge-triggered events.  Also fixed bug where a module could be
 parsed more than once.  Full regression does not pass at this point due to
 new instance6.2.v diagnostic.

 Revision 1.23  2002/11/05 00:20:07  phase1geo
 Adding development documentation.  Fixing problem with combinational logic
 output in report command and updating full regression.

 Revision 1.22  2002/11/02 16:16:20  phase1geo
 Cleaned up all compiler warnings in source and header files.

 Revision 1.21  2002/10/31 23:13:51  phase1geo
 Fixing C compatibility problems with cc and gcc.  Found a few possible problems
 with 64-bit vs. 32-bit compilation of the tool.  Fixed bug in parser that
 lead to bus errors.  Ran full regression in 64-bit mode without error.

 Revision 1.20  2002/10/29 19:57:50  phase1geo
 Fixing problems with beginning block comments within comments which are
 produced automatically by CVS.  Should fix warning messages from compiler.

 Revision 1.19  2002/10/13 13:55:52  phase1geo
 Fixing instance depth selection and updating all configuration files for
 regression.  Full regression now passes.

 Revision 1.18  2002/10/01 13:21:25  phase1geo
 Fixing bug in report output for single and multi-bit selects.  Also modifying
 the way that parameters are dealt with to allow proper handling of run-time
 changing bit selects of parameter values.  Full regression passes again and
 all report generators have been updated for changes.

 Revision 1.17  2002/09/25 05:36:08  phase1geo
 Initial version of parameter support is now in place.  Parameters work on a
 basic level.  param1.v tests this basic functionality and param1.cdd contains
 the correct CDD output from handling parameters in this file.  Yeah!

 Revision 1.16  2002/09/25 02:51:44  phase1geo
 Removing need of vector nibble array allocation and deallocation during
 expression resizing for efficiency and bug reduction.  Other enhancements
 for parameter support.  Parameter stuff still not quite complete.

 Revision 1.15  2002/09/23 01:37:45  phase1geo
 Need to make some changes to the inst_parm structure and some associated
 functionality for efficiency purposes.  This checkin contains most of the
 changes to the parser (with the exception of signal sizing).

 Revision 1.14  2002/09/21 07:03:28  phase1geo
 Attached all parameter functions into db.c.  Just need to finish getting
 parser to correctly add override parameters.  Once this is complete, phase 3
 can start and will include regenerating expressions and signals before
 getting output to CDD file.

 Revision 1.13  2002/09/21 04:11:32  phase1geo
 Completed phase 1 for adding in parameter support.  Main code is written
 that will create an instance parameter from a given module parameter in
 its entirety.  The next step will be to complete the module parameter
 creation code all the way to the parser.  Regression still passes and
 everything compiles at this point.

 Revision 1.12  2002/09/19 05:25:19  phase1geo
 Fixing incorrect simulation of static values and fixing reports generated
 from these static expressions.  Also includes some modifications for parameters
 though these changes are not useful at this point.

 Revision 1.11  2002/09/06 03:05:28  phase1geo
 Some ideas about handling parameters have been added to these files.  Added
 "Special Thanks" section in User's Guide for acknowledgements to people
 helping in project.

 Revision 1.10  2002/08/19 04:34:07  phase1geo
 Fixing bug in database reading code that dealt with merging modules.  Module
 merging is now performed in a more optimal way.  Full regression passes and
 own examples pass as well.

 Revision 1.9  2002/07/18 05:50:45  phase1geo
 Fixes should be just about complete for instance depth problems now.  Diagnostics
 to help verify instance handling are added to regression.  Full regression passes.
*/

