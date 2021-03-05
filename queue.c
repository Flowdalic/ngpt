
  /* -*-c-*- NGPT: O(1) queues
  ** 
  ** $Id: queue.c,v 1.2 2003/01/09 18:20:40 billa Exp $
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

#include "pth_p.h"
#include "node.h"
#include "queue.h"
#include "schedule.h"

intern void queue_dump (struct queue_st *queue)
{
    size_t total;
    struct lnode_st *head, *itr;

    spin_lock (&queue->lock, NULL);
    total = __queue_total (queue);
    debugmsg ("|  Total %u threads\n", total);
    head = &queue->queue;
    for (itr = head; itr->next != head; itr = list_next(itr)) {
      pth_t t = thread_by_node (node_by_lnode (itr));
      debugmsg ("|   thread 0x%p (\"%16.s\"), last native tid =%d\n",
                t, t->name, t->lastrannative);
    }
    spin_unlock (&queue->lock, NULL);
}

