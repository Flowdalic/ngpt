
  /* -*-c-*- NGPT: O(1) priority run queues
  ** 
  ** $Id: pqueue.h,v 1.2 2002/11/25 15:56:51 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** This module contains all the run queues stuff. A runqueue
  ** contains a lock, a number of elements on it, a list of heads [the
  ** prioritized lists] and a bitmap to speed up look up. The nodes in
  ** this case [struct pnode_st] are a wee bit disguised so they
  ** contain two items asides from the list node: the priority and the
  ** pqueue they are on (this speeds access and reduces complexity in
  ** many operations, as they are always consistent with the actual
  ** pnode location).
  **
  ** Heavily based on common sense and Ingo Molnar's O(1) scheduler
  ** for Linux. 
  **
  ** Portions (C) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
  ** Some parts based on portions (C) The Linux Kernel Hackers
  ** (C) 2001 International Business Machines Corporation
  **   Bill Abt <babt@us.ibm.com>
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **      - Implementation
  **

w           After

  
O(1)          O(1)      pth_pqueue_head              __pqueue_first_node
O(1)          O(1)      pth_pqueue_delmax            __pqueue_first_node
O(1)          O(1)      pth_pqueue_init              pqueue_init
O(n)          O(1)      pth_pqueue_insert            __pqueue_add_node
O(n)          ----      pth_pqueue_Qinsert           GONE (unneeded)
O(1)          O(1)      pth_pqueue_elements          __pqueue_total
O(n)          O(1)      pth_pqueue_delete            __pqueue_delete_node

O(n)          O(1)      pth_pqueue_contains          __pqueue_contains
O(n)          O(1)---   pth_pqueue_tail

                          Not needed - used because insert_tail was
                          O(N) before for the New Queue
  
O(1)          ----      pth_pqueue_walk   GONE

O(1)                    pth_pqueue_favorite_prio    Make this top prio?
O(n)          O(1)      pth_pqueue_favorite         __pqueue_delete_node() + __pqueue_add_node_head
  
                        pth_pqueue_increase Hmmm ... for SCHED_OTHER
                        ... hmm

                        void pth_dumpqueue(const char *qn, pth_pqueue_t *q)
  */

#ifndef __ngpt__pqueue_h__
#define __ngpt__pqueue_h__

#include <stddef.h>
#include "list.h"
#include "spinlock.h"
#include "priorities.h"
#include "bitmap.h"
#include "node.h"
#include "debug.h"


  /* Debug stuff */

#define PQUEUE_CHECK_CONSISTENCY 1


  /* Priority queue arrays for scheduling */

#define PRIO_TOTAL IPRIO_MAX
struct pqueue_st
{
  struct lnode_st queue[PRIO_TOTAL]; /* List of queues [one/priority] */
  size_t total;                      /* Total number of nodes */
  struct bitmap_st bitmap;           /* Accelerator for getting first */
  spinlock_t lock;                   /* pqueue's lock */
};


  /* [INTERNAL] Insert a node into the pqueue at beginning of the
  ** queue for it's priority.
  **
  ** This function does no locking on the pqueue.
  **
  ** param node Node to insert
  **
  ** param pqueue Priority queue where to insert it.
  */

static __inline__
void __pqueue_insert (struct node_st *node,
                      struct pqueue_st *pqueue)
{
  list_insert (&node->lnode, &pqueue->queue[node->priority]);
  node_pqueue_set (node, pqueue);
}


  /* [INTERNAL] Insert in the second position of the queue
  ** corresponding to the priority [if the list is empty, it will be
  ** the first].
  **
  ** This function does no locking on the pqueue.
  **
  ** param node Node to insert
  **
  ** param pqueue Priority queue where to insert it.
  */

static __inline__
void __pqueue_insert_second (struct node_st *node,
                             struct pqueue_st *pqueue)
{
  list_insert (&node->lnode, pqueue->queue[node->priority].next);
  node_pqueue_set (node, pqueue);
}


  /* [INTERNAL] Append an node to the queue corresponding to the
  ** priority.
  **
  ** This function does no locking on the pqueue.
  **
  ** param node Node to append.
  **
  ** param pqueue Priority queue where to append.
  */

static __inline__
void __pqueue_insert_tail (struct node_st *node,
                           struct pqueue_st *pqueue)
{
  list_insert_tail (&node->lnode, &pqueue->queue[node->priority]);
  node_pqueue_set (node, pqueue);
}


  /* [INTERNAL] Remove a node from a priority queue.
  **
  ** This function does no locking on the pqueue.
  **
  ** The pqueue is not needed, as the node has a backreference to the
  ** pqueue where it is inserted.
  **
  ** param node Node to remove.
  */

static __inline__
void __pqueue_delete (struct node_st *node)
{
  list_del (&node->lnode);
  node_pqueue_set (node, NULL);
}







  /* Initialize a pqueue that has been zeroed previously.
  **
  ** param pqueue Pointer to the priority queue structure.
  */

static __inline__
void pqueue_init (struct pqueue_st *pqueue)
{
  size_t cnt;
  bitmap_init (&pqueue->bitmap);
  pqueue->total = 0;
  spinlock_init (&pqueue->lock);
  for (cnt = 0; cnt < PRIO_TOTAL; cnt++)
    lnode_init (&pqueue->queue[cnt]);
}


  /* Add a node to the tail of it's priority list in a pqueue.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** param pqueue Priority queue where to append.
  **
  ** param node Node to append.
  */

static __inline__
void __pqueue_append_node (struct pqueue_st *pqueue, struct node_st *node)
{
  if (node_lt_check (node, node_lt_nowhere))
    return;
  if (PQUEUE_CHECK_CONSISTENCY
      && node->bp.pqueue != NULL)
    __debugmsg ("%s (%p, %p): trying to append node to "
                "pqueue being already on pqueue %p\n",
                __FUNCTION__, pqueue, node, node->bp.pqueue);
  pqueue->total++;
  bitmap_set_bit (&pqueue->bitmap, node->priority);
  __pqueue_insert_tail (node, pqueue);
}

static __inline__
void pqueue_append_node (struct pqueue_st *pqueue, struct node_st *node,
                         void *locker)
{
  spin_lock (&pqueue->lock, locker, NULL);
  __pqueue_append_node (pqueue, node);
  spin_unlock (&pqueue->lock, locker);
}


  /* Add a node to the beginning of it's priority list in a pqueue.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** param pqueue Priority queue where to prepend.
  **
  ** param node Node to prepend.
  */

static __inline__
void __pqueue_prepend_node (struct pqueue_st *pqueue, struct node_st *node)
{
  if (node_lt_check (node, node_lt_nowhere))
    return;
  if (PQUEUE_CHECK_CONSISTENCY
      && node->bp.pqueue != NULL)
    __debugmsg ("%s (%p, %p): trying to prepend node to "
                "pqueue being already on pqueue %p\n",
                __FUNCTION__, pqueue, node, node->bp.pqueue);
  pqueue->total++;
  bitmap_set_bit (&pqueue->bitmap, node->priority);
  __pqueue_insert (node, pqueue);
}


static __inline__
void pqueue_prepend_node (struct pqueue_st *pqueue,
                                 struct node_st *node,
                                 void *locker)
{
  spin_lock (&pqueue->lock, locker, NULL);
  __pqueue_prepend_node (pqueue, node);
  spin_unlock (&pqueue->lock, locker);
}


  /* Add a node to the second position of it's priority list in a
  ** pqueue.
  **
  ** If the list is empty [aka: if there is no way to be the second],
  ** then no problem, it will be the first :)
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** param pqueue Priority queue where to prepend.
  **
  ** param node Node to prepend.
  */

static __inline__
void __pqueue_prepend_second_node (struct pqueue_st *pqueue,
                                   struct node_st *node)
{
  if (node_lt_check (node, node_lt_nowhere))
    return;
  if (PQUEUE_CHECK_CONSISTENCY
      && node->bp.pqueue != NULL)
    __debugmsg ("%s (%p, %p): trying to prepend_second node to "
                "pqueue being already on pqueue %p\n",
                __FUNCTION__, pqueue, node, node->bp.pqueue);
  pqueue->total++;
  bitmap_set_bit (&pqueue->bitmap, node->priority);
  __pqueue_insert_second (node, pqueue);
}

static __inline__
void pqueue_prepend_second_node (struct pqueue_st *pqueue,
                                 struct node_st *node,
                                 void *locker)
{
  spin_lock (&pqueue->lock, locker, NULL);
  __pqueue_prepend_second_node (pqueue, node);
  spin_unlock (&pqueue->lock, locker);
}


  /* Remove a node from a pqueue.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** param node Node to remove.
  */

static __inline__
void __pqueue_delete_node (struct node_st *node)
{
  struct pqueue_st *pqueue = node->bp.pqueue;

  if (node_lt_check (node, node_lt_pqueue))
    return;
  if (likely (pqueue))
  {
    pqueue->total--;
    __pqueue_delete (node);
    if (list_empty (&pqueue->queue[node->priority]))
      bitmap_reset_bit (&pqueue->bitmap, node->priority);
  }
  else if (PQUEUE_CHECK_CONSISTENCY)
    __debugmsg ("%s (%p): trying to delete node from "
                "pqueue not being in any pqueue\n",
                __FUNCTION__, node);
}

static __inline__
void pqueue_delete_node (struct node_st *node, void *locker)
{
  struct pqueue_st *pqueue = node->bp.pqueue;

  if (node_lt_check (node, node_lt_pqueue))
    return;
  if (likely (pqueue != NULL))
  {
    spin_lock (&pqueue->lock, locker, NULL);
    __pqueue_delete_node (node);
    spin_unlock (&pqueue->lock, locker);
  }
  else if (PQUEUE_CHECK_CONSISTENCY)
    __debugmsg ("%s (%p): trying to delete node from "
                "pqueue not being in any pqueue\n",
                __FUNCTION__, node);
}


  /* Return the first node (priority wise).
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** This will NOT remove it from the list!.
  **
  ** param pqueue Priority queue where to get the first node from.
  */

static __inline__
struct node_st * __pqueue_first_node (struct pqueue_st *pqueue)
{
  struct node_st *node = NULL;
  
  if (likely (pqueue->total != 0))
  {
    unsigned prio = bitmap_ffms_bit_set (&pqueue->bitmap);
    struct lnode_st *lnode = pqueue->queue[prio].next;
    if (PQUEUE_CHECK_CONSISTENCY
        && lnode == &pqueue->queue[prio])
    {
      __debugmsg ("%s (%p): BUG: pqueue->total != 0 (%u) but "
                  "head.next == &head!!\n",
                  __FUNCTION__, pqueue, pqueue->total);
      stack_dump();
      goto exit;
    }
    node = list_entry (lnode, struct node_st, lnode);
  }
  else if (PQUEUE_CHECK_CONSISTENCY)
  {
    __debugmsg ("%s (%p, %p): trying to get first node from "
                "empty pqueue\n",
                __FUNCTION__, pqueue, node);
    stack_dump();
  }
 exit:
  return node;
}

static __inline__
struct node_st * pqueue_first_node (struct pqueue_st *pqueue, void *locker)
{
  struct node_st *node = NULL;
  spin_lock (&pqueue->lock, locker, NULL);
  node = __pqueue_first_node (pqueue);
  spin_unlock (&pqueue->lock, locker);
  return node;
}


  /* Set the priority of a node
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** This works be the node inserted on a list or not. Watch out! It
  ** will be moved even if it is the first in the list.
  **
  ** If the prio does not change, we do nothing; if it increases, we
  ** append to the new list; if it decreases, we prepend to the new
  ** list [this is to implement the needed behaviour that POSIX states
  ** in 1003.1 2001 p46 bullet 8].
  **
  ** If the node is in a queue and not in a pqueue, this will cause
  ** corruption. You are warned. Check out
  ** node.h:NODE_CHECK_CONSISTENCY.
  **
  ** param node Node whose priority is to be changed.
  **
  ** param new_priority New priority for the node.
  */

static __inline__
void __pqueue_node_priority_set (struct node_st *node, int new_priority)
{
  if (unlikely (node->priority == new_priority))
    return;
  if (node->bp.pqueue != NULL)
  {
    struct pqueue_st *pqueue = node->bp.pqueue;
    if (node_lt_check (node, node_lt_pqueue))
      return;
    list_del (&node->lnode);
    if (list_empty (&pqueue->queue[node->priority]))
      bitmap_reset_bit (&pqueue->bitmap, node->priority);
    if (node->priority < new_priority)
      list_insert_tail (&node->lnode, &pqueue->queue[new_priority]);
    else
      list_insert (&node->lnode, &pqueue->queue[new_priority]);
    bitmap_set_bit (&pqueue->bitmap, new_priority);
  }
  node->priority = new_priority;
}


  /* Get the priority of a node
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** param node Node whose priority we want to get.
  */

static __inline__
int __pqueue_node_priority_get (struct node_st *node)
{
  return node->priority;
}


  /* Report the number of elements in the priority queue.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** I don't think the locking version is really needed here, as an
  ** integer is, after all, an atomic type, so I don't lock
  ** it. However, I keep the interface - just in case someday the
  ** implementation changes.
  **
  ** param pqueue Priority queue we want to query.
  */

static __inline__
size_t __pqueue_total (const struct pqueue_st *pqueue)
{
  return pqueue->total;
}

static __inline__
size_t pqueue_total (struct pqueue_st *pqueue)
{
  size_t total;
  total = __pqueue_total (pqueue);
  return total;
}


  /* Report if a priority queue contains a node.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** For speed considerations, this is why we use the backtracking
  ** [keeping the pointer to the pqueue in the node]. In the future,
  ** if we don't require it, we could remove all the support for it
  ** [that'd be great, by the way] :)
  **
  ** param pqueue Priority queue that we want to check.
  **
  ** param node Node we want to test for being in the pqueue.
  */

static __inline__
int __pqueue_contains (const struct pqueue_st *pqueue,
                       const struct node_st *node)
{
  if (node_lt_check (node, node_lt_pqueue))
    return 0;
  return node->bp.pqueue == pqueue;
}

static __inline__
int pqueue_contains (struct pqueue_st *pqueue,
                     const struct node_st *node,
                     void *locker)
{
  int result;
  if (node_lt_check (node, node_lt_pqueue))
    return 0;
  spin_lock (&pqueue->lock, locker, NULL);
  result = node->bp.pqueue == pqueue;
  spin_unlock (&pqueue->lock, locker);
  return result;
}


  /* Make a node be the second one in the head of its priority list.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** Basically, when we want to make a node have preference over
  ** others [ie: make it the next one to be picked], we put it the
  ** second [as the first is always the current one].
  **
  ** param node Node we want to favor.
  */

static __inline__
void __pqueue_favor (struct node_st *node)
{
  struct pqueue_st *pqueue = node->bp.pqueue;
  int priority = node->priority;
  struct lnode_st *head = &pqueue->queue[priority];

  if (pqueue == NULL)
    return;
  if (node_lt_check (node, node_lt_pqueue))
    return;
    /* List empty? strange, shout, do nothing */
  if (PQUEUE_CHECK_CONSISTENCY
      && list_empty (head))
  {
    __debugmsg ("%s (%p): BUG: favor node in an empty list\n",
                __FUNCTION__, node);
    return;
  }
    /* Already first? do nothing */
  if (head->next == &node->lnode)
    return;
    /* Already second? do nothing */
  if (head->next->next == &node->lnode)
    return;
    /* Okay, move it to pqueue->queue[priority].next->next */
  list_del (&node->lnode);
  list_insert (&node->lnode, head->next);
}

static __inline__
void pqueue_favor (struct node_st *node, void *locker)
{
  if (likely (node->bp.pqueue != NULL))
  {
    spin_lock (&node->bp.pqueue->lock, locker, NULL);
    __pqueue_favor (node);
    spin_unlock (&node->bp.pqueue->lock, locker);
  }
}


  /* Get next node
  **
  ** This is basically the support for iterating in the list from
  ** the first item in the list w/ maximum priority down to the last
  ** one with minimal priority.
  **
  ** When the iterator reaches the end of a priority list, it advances
  ** to the next with lower priority that is not empty.
  **
  ** param node Current iterator [pqueue pointer embedded on it.
  */

static __inline__
struct node_st * __pqueue_get_next_node (struct node_st *node)
{
  struct pqueue_st *pqueue = node->bp.pqueue;
  struct node_st *next = NULL;
  
  if (PQUEUE_CHECK_CONSISTENCY && pqueue == NULL) {
    __debugmsg ("%s (%p): BUG: trying to get next node from a node "
                "that is not in any list\n", __FUNCTION__, node);
    return NULL;
  }
  if (node_lt_check (node, node_lt_pqueue))
    return NULL;

    /* node is the last of his priority list? if no, get the next one
    ** in the list; if yes, just advance to the next non-empty list
    ** and get the first one. If the actual's node priority was zero,
    ** then it means there are no more lists to search on, so we
    ** return NULL.
    */
  if (likely (node->lnode.next != &pqueue->queue[node->priority]))
    next = node_by_lnode (node->lnode.next);
  else if (node->priority > 0)
  {
    int priority_cnt;
    for (priority_cnt = node->priority - 1; priority_cnt >= 0; priority_cnt--)
      if (!list_empty (&pqueue->queue[priority_cnt]))
      {
        next = node_by_lnode (pqueue->queue[priority_cnt].next);
        break;
      }
  }
  return next;
}

#endif /* __ngpt_pqueue_h__ */
