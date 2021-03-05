
  /* -*-c-*- NGPT: O(1) priority run queues
  ** 
  ** $Id: pqueue.c,v 1.2 2003/01/09 18:20:40 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** This module contains all the run queues stuff.
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
  */

#include "pth_p.h"
#include "pqueue.h"
#include "schedule.h"

intern void pqueue_dump (struct pqueue_st *pqueue)
{
    size_t prio_cnt, total;
    struct lnode_st *head, *itr;

    spin_lock (&pqueue->lock, NULL, NULL);
    total = __pqueue_total (pqueue);
    for (prio_cnt = 0; prio_cnt < PRIO_TOTAL; prio_cnt++)
    {
      /* get head of list for priority */
      head = &pqueue->queue[prio_cnt];
      /* Skip showing empty lists */
      if (list_empty(head))
	break;
      /* Display list elements for priority */
      debugmsg ("|  Priority %u, total queued %u threads\n", prio_cnt, total);
      for (itr = head; itr->next != head; itr = list_next(itr)) {
          pth_t t = thread_by_node (node_by_lnode (itr));
          debugmsg ("|   thread 0x%p (\"%16.s\"), last native tid =%d\n",
                    t, t->name, t->lastrannative);
      }
    }
    spin_unlock (&pqueue->lock, NULL);
}

