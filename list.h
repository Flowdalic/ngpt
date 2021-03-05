
  /* -*-c-*- NGPT: Linked lists
  ** 
  ** $Id: list.h,v 1.3 2002/10/09 14:54:06 billa Exp $
  **
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **
  ** This file is part of NGPT, a non-preemptive thread scheduling
  ** library which can be found at http://www.ibm.com/developer.
  **
  ** This library is free software; you can redistribute it and/or
  ** modify it under the terms of the GNU Lesser General Public
  ** License as published by the Free Software Foundation; either
  ** version 2.1 of the License, or (at your option) any later version.
  **
  ** This library is distributed in the hope that it will be useful,
  ** but WITHOUT ANY WARRANTY; without even the implied warranty of
  ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  ** Lesser General Public License for more details.
  **
  ** You should have received a copy of the GNU Lesser General Public
  ** License along with this library; if not, write to the Free Software
  ** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  ** USA, or contact Bill Abt <babt@us.ibm.com>
  **
  **
  ** Implemented following as a great detailed reference the
  ** linux/include/list.h file. Thus the likeliness of the code [not
  ** to say identicalness]. 
  */

#ifndef __ngpt_list_h__
#define __ngpt_list_h__

  /* Double linked list */

struct lnode_st {
    struct lnode_st *next, *prev;
};

  /* Statically (compile time) initialize a node */

#define lnode_INIT(node) { &(node), &(node) }

  /* Initialize an existing node */

static __inline__ void lnode_init(struct lnode_st *node)
{
    node->next = node->prev = node;
}


  /* Insert an entry into two entries (consecutive, please)
  **
  ** INTERNAL! DO NOT USE DIRECTLY [assumes n and p are consecutive]
  **
  ** param e Entry to insert
  **
  ** param p Previous entry
  **
  ** param n Next entry
  */

static __inline__
    void __list_insert(struct lnode_st *e, struct lnode_st *p,
		       struct lnode_st *n)
{
    e->next = n;
    e->prev = p;
    p->next = e;
    n->prev = e;
}


  /* Insert e between w and w->next */

static __inline__ void list_insert(struct lnode_st *e, struct lnode_st *w)
{
    __list_insert(e, w, w->next);
}


  /* Insert e between w->prev and w */

static __inline__
    void list_insert_tail(struct lnode_st *e, struct lnode_st *w)
{
    __list_insert(e, w->prev, w);
}


  /* Insert list l between w and w->next */

static __inline__
    void list_insert_list(struct lnode_st *l, struct lnode_st *w)
{
    struct lnode_st *ll,	/* list's first, list's last */
    *wn;			/* where, where next */

    ll = l->prev;
    wn = w->next;

    w->next = l;
    l->prev = w;
    ll->next = wn;
    wn->prev = ll;
}


  /* Helper for removal from list
  **
  ** INTERNAL! DO NOT USE DIRECTLY
  **
  ** Will link p with n, thus, discarding everyone in the middle.
  */

static __inline__ void __list_del(struct lnode_st *p, struct lnode_st *n)
{
    n->prev = p;
    p->next = n;
}


  /* Delete from the list
  **
  ** param e Item to remove
  */

static __inline__ void list_del(struct lnode_st *e)
{
    __list_del(e->prev, e->next);
}

  /* Delete an entry from the list, then reinitialize it
  **
  ** param e Entry to delete and reinitialize
  */

static __inline__ void list_del_init(struct lnode_st *e)
{
    __list_del(e->prev, e->next);
    lnode_init(e);
}

  /* Check if the list is empty
  **
  ** param e pointer to entry or list head
  **
  ** return 0 if not empty, !0 if empty
  */

static __inline__ int list_empty(struct lnode_st *e)
{
    return e->next == e;
}


  /* Get the pointer to the struct this entry is part of
  **
  ** param e Pointer to the entry that is in the list
  ** param t Type of the struct the entry is embedded in
  ** param m Name of the member the entry is embedded as
  **
  ** return Pointer to an structure of type t that contains the given
  **        entry. 
  */

#define list_entry(e, t, m) \
	((t *) ( (char *)(e) - (unsigned long) (&((t *)0)->m) ) )


  /* Return !0 if node is the last one
  **
  ** param n node to test for last-ness.
  **
  ** param f First node in the list (list head).
  **
  ** return 0 if not the last one, !0 if last one
  */

static __inline__
    int list_node_is_tail(const struct lnode_st *n, const struct lnode_st *f)
{
    return n->next == f;
}


  /* Get the next node in the list
  **
  ** param e Pointer to the entry that is in the list
  **
  ** return Pointer to the next element in the list
  */

static __inline__ struct lnode_st * list_next (const struct lnode_st *lnode)
{
  return lnode->next;
}


#endif				/* __ngpt_list_h__ */
