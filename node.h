
  /* -*-c-*- NGPT: Common queue and pqueue node
  ** 
  ** $Id: node.h,v 1.1 2002/10/09 15:11:52 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** Portions (C) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
  ** Some parts based on portions (C) The Linux Kernel Hackers
  ** (C) 2001 International Business Machines Corporation
  **   Bill Abt <babt@us.ibm.com>
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **      - Implementation
  */

#ifndef __ngpt__node_h__
#define __ngpt__node_h__

#include "list.h"
#include "debug.h"

  /* Debug switches [0/1] to disable/enable */

#define NODE_CHECK_CONSISTENCY 1


struct pqueue_st;
struct queue_st;

  /* A node in a list
  **
  ** We use a queue/pqueue backpointer to be able to fast track in
  ** what list this node is inserted.
  **
  ** For pqueues, we also keep here the priority, why?  because many
  ** functions need it to operate fast and accurately. Basically, a
  ** pnode can backtrack in O(1) time what is the pqueue where it is
  ** and in which queue is it inserted.
  */

enum node_lt_e {    /* list type */
  node_lt_nowhere,  /* node is in no list */
  node_lt_queue,    /* node is in a queue */
  node_lt_pqueue    /* node is in a priority queue */
};

struct node_st
{
  struct lnode_st lnode;   /* What gets inserted in the list */
  union {
    struct pqueue_st *pqueue;
    struct queue_st *queue;
  } bp;
  int priority;
#if NODE_CHECK_CONSISTENCY == 1
  enum node_lt_e node_lt;
#endif
};


  /* Fill in non-zero data (assumes it has been zeroed already) */

static __inline__
void node_init (struct node_st *node)
{
  lnode_init (&node->lnode);
}


  /* Get the address of a node knowing it's lnode [list node] */

static __inline__
struct node_st * node_by_lnode (struct lnode_st *lnode)
{
  return lnode? list_entry (lnode, struct node_st, lnode) : NULL;
}


  /* Set a node's backreference to a pqueue */

static __inline__
void node_pqueue_set (struct node_st *node, struct pqueue_st *pqueue)
{
  node->bp.pqueue = pqueue;
#if NODE_CHECK_CONSISTENCY == 1
  node->node_lt = pqueue? node_lt_pqueue : node_lt_nowhere;
#endif /* NODE_CHECK_CONSISTENCY == 0 */
}  


  /* Set a node's backreference to a queue */

static __inline__
void node_queue_set (struct node_st *node, struct queue_st *queue)
{
  node->bp.queue = queue;
#if NODE_CHECK_CONSISTENCY == 1
  node->node_lt = queue? node_lt_queue : node_lt_nowhere;
#endif /* NODE_CHECK_CONSISTENCY == 0 */
}  


  /* Check node's consistency regarding list type */

static __inline__
int node_lt_check (const struct node_st *node, enum node_lt_e node_lt)
{
#if NODE_CHECK_CONSISTENCY == 0
  return 0;
#else /* NODE_CHECK_CONSISTENCY == 1 */
  if (node->node_lt != node_lt)
  {
    __debugmsg ("%s (%p, %d): BUG: node is in a list type %d, "
                "fails consistency check\n",
                __FUNCTION__, node, node_lt, node->node_lt);
    stack_dump();
    return 1;
  }
  return 0;
#endif /* NODE_CHECK_CONSISTENCY == 0 */
}  

#endif /* __ngpt_node_h__ */
