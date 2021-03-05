
  /* -*-c-*- NGPT: O(1) queues
  ** 
  ** $Id: queue.h,v 1.1 2002/10/09 15:10:12 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** This module contains simple queues w/ no priority ordering [aka
  ** FIFO/LIFO style].
  **
  ** Portions (C) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
  ** Some parts based on portions (C) The Linux Kernel Hackers
  ** (C) 2001 International Business Machines Corporation
  **   Bill Abt <babt@us.ibm.com>
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **      - Implementation
  */

#ifndef __ngpt__queue_h__
#define __ngpt__queue_h__

#include <stddef.h>
#include "debug.h"
#include "list.h"
#include "spinlock.h"
#include "node.h"

  /* Debug stuff */

#define QUEUE_CHECK_CONSISTENCY 1


  /* Priority queue arrays for scheduling */

struct queue_st
{
  struct lnode_st queue;
  size_t total;
  spinlock_t lock;
};


  /* [INTERNAL] Insert a node at beginning of the queue.
  **
  ** This function does no locking on the queue.
  **
  ** param node Node to insert
  **
  ** param queue Queue where to insert it.
  */

static __inline__
void __queue_insert (struct node_st *node,
                     struct queue_st *queue)
{
  list_insert (&node->lnode, &queue->queue);
  node_queue_set (node, queue);
}


  /* [INTERNAL] Append an node to the queue.
  **
  ** This function does no locking on the queue.
  **
  ** param node Node to append.
  **
  ** param queue Queue where to append.
  */

static __inline__
void __queue_insert_tail (struct node_st *node,
                          struct queue_st *queue)
{
  list_insert_tail (&node->lnode, &queue->queue);
  node_queue_set (node, queue);
}


  /* [INTERNAL] Remove a node from a queue.
  **
  ** This function does no locking on the queue.
  **
  ** The queue is not needed, as the node has a backreference to the
  ** queue where it is inserted.
  **
  ** param node Node to remove.
  */

static __inline__
void __queue_delete (struct node_st *node)
{
  list_del (&node->lnode);
  node_queue_set (node, NULL);
}


  /* Initialize a queue that has been zeroed previously.
  **
  ** param queue Pointer to the priority queue structure.
  */

static __inline__
void queue_init (struct queue_st *queue)
{
  spinlock_init (&queue->lock);
  lnode_init (&queue->queue);
}


  /* Add a node to the tail of the queue.
  **
  ** The functions prefixed with __ do not lock the queue.
  **
  ** param queue Queue where to append.
  **
  ** param node Node to append.
  */

static __inline__
void __queue_append_node (struct queue_st *queue, struct node_st *node)
{
  if (node_lt_check (node, node_lt_nowhere))
    return;
  if (QUEUE_CHECK_CONSISTENCY
      && node->bp.queue != NULL)
    __debugmsg ("%s (%p, %p): trying to append node to "
                "queue being already on queue %p\n",
                __FUNCTION__, queue, node, node->bp.queue);
  queue->total++;
  __queue_insert_tail (node, queue);
}

static __inline__
void queue_append_node (struct queue_st *queue, struct node_st *node,
                        void *locker)
{
  spin_lock (&queue->lock, locker, NULL);
  __queue_append_node (queue, node);
  spin_unlock (&queue->lock, locker);
}


  /* Report the number of elements in the queue.
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** I don't think the locking version is really needed here, as an
  ** integer is, after all, an atomic type, so I don't lock
  ** it. However, I keep the interface - just in case someday the
  ** implementation changes.
  **
  ** param queue Queue we want to query.
  */

static __inline__
size_t __queue_total (const struct queue_st *queue)
{
  return queue->total;
}

static __inline__
size_t queue_total (struct queue_st *queue)
{
  size_t total;
  total = __queue_total (queue);
  return total;
}


  /* Return the first node
  **
  ** The functions prefixed with __ do not lock the pqueue.
  **
  ** This will NOT remove it from the list! Also, this looks more
  ** complex than it really is [just get me the head.next], but it is
  ** because of the consistency checks that get optimized out when
  ** QUEUE_CHECK_CONSISTENCY==0.
  **
  ** param queue Queue where to get the first node from.
  */

static __inline__
struct node_st * __queue_first_node (struct queue_st *queue)
{
  struct node_st *node = NULL;
  
  if (likely (queue->total != 0))
  {
    struct lnode_st *lnode = queue->queue.next;
    if (QUEUE_CHECK_CONSISTENCY
        && lnode == &queue->queue)
      __debugmsg ("%s (%p): BUG: queue->total != 0 but head.next == &head!!",
                  __FUNCTION__, queue);
    node = list_entry (lnode, struct node_st, lnode);
  }
  else if (QUEUE_CHECK_CONSISTENCY)
    __debugmsg ("%s (%p, %p): trying to get first node from "
                "empty queue\n",
                __FUNCTION__, queue, node);
  return node;
}

static __inline__
struct node_st * queue_first_node (struct queue_st *queue, void *locker)
{
  struct node_st *node = NULL;
  spin_lock (&queue->lock, locker, NULL);
  node = __queue_first_node (queue);
  spin_unlock (&queue->lock, locker);
  return node;
}


  /* Report if a queue contains a node.
  **
  ** The functions prefixed with __ do not lock the queue.
  **
  ** For speed considerations, this is why we use the backtracking
  ** [keeping the pointer to the queue in the node]. In the future,
  ** if we don't require it, we could remove all the support for it
  ** [that'd be great, by the way] :)
  **
  ** param queue Priority queue that we want to check.
  **
  ** param node Node we want to test for being in the queue.
  */

static __inline__
int __queue_contains (const struct queue_st *queue,
                      const struct node_st *node)
{
  if (node_lt_check (node, node_lt_queue))
    return 0;
  return node->bp.queue == queue;
}

static __inline__
int queue_contains (struct queue_st *queue,
                    const struct node_st *node,
                    void *locker)
{
  int result;
  if (node_lt_check (node, node_lt_queue))
    return 0;
  spin_lock (&queue->lock, locker, NULL);
  result = node->bp.queue == queue;
  spin_unlock (&queue->lock, locker);
  return result;
}


  /* Remove a node from a queue.
  **
  ** The functions prefixed with __ do not lock the queue.
  **
  ** param node Node to remove.
  */

static __inline__
void __queue_delete_node (struct node_st *node)
{
  struct queue_st *queue = node->bp.queue;

  if (node_lt_check (node, node_lt_queue))
    return;
  if (likely (queue))
  {
    queue->total--;
    __queue_delete (node);
  }
  else if (QUEUE_CHECK_CONSISTENCY)
    __debugmsg ("%s (%p): trying to delete node from "
                "queue not being in any queue\n",
                __FUNCTION__, node);
}

static __inline__
void queue_delete_node (struct node_st *node, void *locker)
{
  struct queue_st *queue = node->bp.queue;

  if (node_lt_check (node, node_lt_queue))
    return;
  if (likely (queue != NULL))
  {
    spin_lock (&queue->lock, locker, NULL);
    __queue_delete_node (node);
    spin_unlock (&queue->lock, locker);
  }
  else if (QUEUE_CHECK_CONSISTENCY)
    __debugmsg ("%s (%p): trying to delete node from "
                "queue not being in any queue\n",
                __FUNCTION__, node);
}

#endif /* __ngpt__queue_h__ */
