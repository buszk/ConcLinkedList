/*
 *  linkedlist.h
 *  interface for the list
 *
 */
#ifndef LLIST_H_ 
#define LLIST_H_


#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>

#include "atomic_ops_if.h"

#ifdef DEBUG
#define IO_FLUSH                        fflush(NULL)
#endif

typedef intptr_t val_t;

typedef struct node 
{
	struct node *next;
	val_t data;
  struct node *padding;
} node_t;

typedef struct fl_node {
  node_t *padding_1;
  val_t padding_2;
  fl_node *next;
} fl_node_t;

typedef struct llist 
{
	node_t *head;
	uint32_t size;
  uint64_t fl;
} llist_t;

inline fl_node_t* get_fl_head(uint64_t n) {
  return (fl_node_t*)(n & 0xffffffffffff);
}

inline uint16_t get_fl_aba(uint64_t n) {
  return (uint16_t) ((n &0xffff000000000000) >> (48));
}

inline uint64_t get_fl(uint16_t aba, fl_node_t* ptr) {
  return ((uint64_t)aba << 48) | (uintptr_t)ptr;
}

inline int is_marked_ref(node_t * i);
inline node_t * unset_mark(node_t * i);
inline node_t * set_mark(node_t * i);
inline node_t * get_unmarked_ref(node_t * w);
inline node_t * get_marked_ref(node_t * w);


llist_t* list_new();
//return 0 if not found, positive number otherwise
int list_contains(llist_t *the_list, val_t val);
//return 0 if value already in the list, positive number otherwise
node_t* list_add(llist_t *the_list, val_t val);
//return 0 if value already in the list, positive number otherwise
void list_remove(llist_t *the_list, val_t val);
void list_delete(llist_t *the_list);
int list_size(llist_t *the_list);
int list_check(llist_t *the_list);
int list_check_flist(llist_t *the_list);


node_t* new_node(val_t val, node_t* next);
node_t* list_search(llist_t* the_list, val_t val, node_t** left_node);

/*
 *  linkedlist.c
 *
 *  Description:
 *   Lock-free linkedlist implementation of Harris' algorithm
 *   "A Pragmatic Implementation of Non-Blocking Linked Lists" 
 *   T. Harris, p. 300-314, DISC 2001.
 */

#include "linkedlist.h"

/*
 * The five following functions handle the low-order mark bit that indicates
 * whether a node is logically deleted (1) or not (0).
 *  - is_marked_ref returns whether it is marked, 
 *  - (un)set_marked changes the mark,
 *  - get_(un)marked_ref sets the mark before returning the node.
 */
inline int
is_marked_ref(node_t * i) 
{
  return (int) (((uintptr_t)i) & 0x1L);
}

inline node_t *
unset_mark(node_t * i)
{
  uintptr_t p = (uintptr_t) i;
  p &= ~0x1L;
  return (node_t *) p;
}

inline node_t *
set_mark(node_t * i) 
{
  uintptr_t p = (uintptr_t) i;
  p |= 0x1L;
  return (node_t *) p;
}

inline node_t *
get_unmarked_ref(node_t * w) 
{
  return (node_t *) (((uintptr_t)w) & ~0x1L);
}

inline node_t *
get_marked_ref(node_t * w) 
{
  return (node_t *) (((uintptr_t)w) | 0x1L);
}

node_t* new_node(val_t val, node_t *next)
{
  //printf("New node method\n");
  // allocate node
  node_t* node = (node_t*) malloc(sizeof(node_t));
  node->data = val;
  node->next = next;
  return node;
}

llist_t* list_new()
{
  //printf("Create list method\n");
  // allocate list
  llist_t* the_list = (llist_t*) malloc(sizeof(llist_t));

  // now need to create the sentinel node
  the_list->head = NULL;
  the_list->size = 0;
  the_list->fl = 0;
  return the_list;
}

void list_delete(llist_t *the_list)
{
  // not for now
}

int list_size(llist_t* the_list) 
{ 
  return the_list->size; 
} 

/*
 * list_add inserts a new node with the given value val in the list
 * (if the value was absent) or does nothing (if the value is already present).
 */

node_t* list_add(llist_t *the_list, val_t val)
{
  
  fl_node_t *fl_head, *fl_next;
  node_t *left, *unmarked;
  uint16_t fl_aba;
  uint64_t fl_last, fl;
  fl_head = fl_next = NULL;
  left = unmarked = NULL;

  do {
    fl_last = the_list->fl;
    fl_head = get_fl_head(fl_last);
    fl_aba = get_fl_aba(fl_last);
    fl_next = fl_head ? fl_head->next : NULL;
    fl = get_fl(fl_aba+1, fl_next);
  } while(fl_head && CAS_PTR(&(the_list->fl), fl_last, fl) != fl_last);


  if (fl_head) {
    left = (node_t*) fl_head;
    if (is_marked_ref(left->next)) {
      
      left->next = get_unmarked_ref(left->next);
      left->data = val;
      FAI_U32(&(the_list->size));
      return left;
    }
    fprintf(stderr, "ERROR3\n");
  }
  node_t *new_elem = new_node(val, NULL);
  do {
    left = the_list->head;
    new_elem->next = left;
  } while(CAS_PTR(&the_list->head, left, new_elem) != left);
  FAI_U32(&(the_list->size));
  return new_elem;
}

/*
 * list_remove deletes a node with the given value val (if the value is present) 
 * or does nothing (if the value is already present).
 * The deletion is logical and consists of setting the node mark bit to 1.
 */
void list_remove(llist_t *the_list, node_t* node)
{
  fl_node_t *fl_node, *fl_head;
  uint64_t fl_last, fl;
  uint16_t fl_aba;
  
  if (node->next == get_marked_ref(node->next)) 
    return;
  
  node->next = get_marked_ref(node->next);
  fl_node = (fl_node_t*) node;

  while (true) {
    fl_last = the_list->fl;
    fl_head = get_fl_head(fl_last);
    fl_aba = get_fl_aba(fl_last);
    fl_node->next = fl_head;
    fl = get_fl(fl_aba+1, fl_node);
    if (CAS_PTR(&(the_list->fl), fl_last, fl) == fl_last){
      FAD_U32(&(the_list->size));
      return;
    }
  }
}

int list_check(llist_t *the_list) {
  node_t *n, *unmarked;
  size_t size;
  n = the_list->head;
  size = 0;
  while (n)
  {
    unmarked = get_unmarked_ref(n->next);
    if (n->next == unmarked) size ++;
    n = unmarked;
  }
  return size;
}

void list_print(llist_t *the_list) {
  printf("free list head: [%016lx] \n", (uintptr_t)get_fl_head(the_list->fl));
  node_t *n, *unmarked;
  n = the_list->head;
  while (n)
  {
    unmarked = get_unmarked_ref(n->next);
    printf("[%016lx] next: %016lx data: %08lx pad: %015lx\n", (uintptr_t)n, (uintptr_t)n->next, (uintptr_t)n->data, (uintptr_t)n->padding);
    n = unmarked;
  }
}

int list_check_flist(llist_t *the_list) {
  node_t *n, *unmarked;
  size_t size;
  n = the_list->head;
  size = 0;
  while (n)
  {
    unmarked = get_unmarked_ref(n->next);
    if (n->next != unmarked) size ++;
    n = unmarked;
  }
  return size;
}

#endif
