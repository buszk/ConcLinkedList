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

template <typename T>
struct node
{
public:
  T data;
  node<T> *next;

  node<T>(T, node<T>*);

};

template <typename T, T T_MIN, T T_MAX>
class llist{
private:

  node<T> *head;
  node<T> *tail;
  uint32_t _size;

  /*
  * The five following functions handle the low-order mark bit that indicates
  * whether a node is logically deleted (1) or not (0).
  *  - is_marked_ref returns whether it is marked, 
  *  - (un)set_marked changes the mark,
  *  - get_(un)marked_ref sets the mark before returning the node.
  */

  inline int 
  is_marked_ref(node<T> * i) 
  {
    return (int) (((uintptr_t)i) & 0x1L);
  }

  inline node<T> *
  unset_mark(node<T> * i)
  {
    uintptr_t p = (uintptr_t) i;
    p &= ~0x1L;
    return (node<T> *) p;
  }

  inline node<T> *
  set_mark(node<T> * i) 
  {
    uintptr_t p = (uintptr_t) i;
    p |= 0x1L;
    return (node<T> *) p;
  }

  inline node<T> *
  get_unmarked_ref(node<T> * w) 
  {
    return (node<T> *) (((uintptr_t)w) & ~0x1L);
  }

  inline node<T> *
  get_marked_ref(node<T> * w) 
  {
    return (node<T> *) (((uintptr_t)w) | 0x1L);
  }

public:

  llist();
  ~llist();
  //return 0 if not found, positive number otherwise
  int contains(T val);
  //return 0 if value already in the list, positive number otherwise
  int add(T val);
  //return 0 if value already in the list, positive number otherwise
  int remove(T val);
  int size();


  node<T>* search(T val, node<T>** left_node);
};


/*
 * list_search looks for value val, it
 *  - returns right_node owning val (if present) or its immediately higher 
 *    value present in the list (otherwise) and 
 *  - sets the left_node to the node owning the value immediately lower than val. 
 * Encountered nodes that are marked as logically deleted are physically removed
 * from the list, yet not garbage collected.
 */

template<typename T, T T_MIN, T T_MAX>
node<T>* llist<T, T_MIN, T_MAX>::search(T val, node<T>** left_node) 
{
  node<T> *left_node_next, *right_node;
  left_node_next = right_node = NULL;
  while(1) {
    node<T> *t = head;
    node<T> *t_next = head->next;
    while (is_marked_ref(t_next) || (t->data < val)) {
      if (!is_marked_ref(t_next)) {
        (*left_node) = t;
        left_node_next = t_next;
      }
      t = get_unmarked_ref(t_next);
      if (t == tail) break;
      t_next = t->next;
    }
    right_node = t;

    if (left_node_next == right_node){
      if (!is_marked_ref(right_node->next))
         return right_node;
    }
    else{
      if (CAS_PTR(&((*left_node)->next), left_node_next, right_node) == left_node_next) {
        if (!is_marked_ref(right_node->next))
          return right_node;
      }
    }
  }
}

/*
 * list_contains returns a value different from 0 whether there is a node in the list owning value val.
 */
template<typename T, T T_MIN, T T_MAX>
int llist<T, T_MIN, T_MAX>::contains(T val)
{
  //printf("Contains method\n");
  node<T>* iterator = get_unmarked_ref(head->next); 
  while(iterator != tail){ 
    if (!is_marked_ref(iterator->next) && iterator->data >= val){ 
      // either we found it, or found the first larger element
      if (iterator->data == val) return 1;
      else return 0;
    }

    // always get unmarked pointer
    iterator = get_unmarked_ref(iterator->next);
  }  
  return 0; 
}

template<typename T>
node<T>::node(T val, node<T> *next)
{
  //printf("New node method\n");
  // allocate node
  data = val;
  next = next;
}
template<typename T, T T_MIN, T T_MAX>
llist<T, T_MIN, T_MAX>::llist()
{
  //printf("Create list method\n");
  // now need to create the sentinel node
  head = new node<T>(T_MIN, NULL);
  tail = new node<T>(T_MAX, NULL);
  head->next = tail;
  _size = 0;
}

template<typename T, T T_MIN, T T_MAX>
llist<T, T_MIN, T_MAX>::~llist()
{
  // not for now
}

template<typename T, T T_MIN, T T_MAX>
int llist<T, T_MIN, T_MAX>::size() 
{ 
  return _size; 
} 

/*
 * list_add inserts a new node with the given value val in the list
 * (if the value was absent) or does nothing (if the value is already present).
 */
template<typename T, T T_MIN, T T_MAX>
int llist<T, T_MIN, T_MAX>::add(T val)
{
  node<T> *right, *left;
  right = left = NULL;
  node<T> *new_elem = new node<T>(val, NULL);
  while(1){
    right = search(val, &left);
    if (right != tail && right->data == val){
      return 0;
    }
    new_elem->next = right;
    if (CAS_PTR(&(left->next), right, new_elem) == right){
      FAI_U32(&(_size));
      return 1;
    }
  }
}

/*
 * list_remove deletes a node with the given value val (if the value is present) 
 * or does nothing (if the value is already present).
 * The deletion is logical and consists of setting the node mark bit to 1.
 */
template<typename T, T T_MIN, T T_MAX>
int llist<T, T_MIN, T_MAX>::remove(T val)
{
  node<T>* right, *left, *right_succ;
  right = left = right_succ = NULL;
  while(1){
    right = search(val, &left);
    // check if we found our node
    if (right == tail || right->data != val){
      return 0;
    }
    right_succ = right->next;
    if (!is_marked_ref(right_succ)){
      if (CAS_PTR(&(right->next), right_succ, get_marked_ref(right_succ)) == right_succ){
        FAD_U32(&_size);
        return 1;
      }
    }
  }
  // we just logically delete it, someone else will invoke search and delete it
}


#endif
