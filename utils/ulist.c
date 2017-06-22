/* This is generic list processing for unit or simple objects like simple variable
 * Written by JJL 
 * http://www.dreamincode.net/code/snippet6687.htm 
 *
 * Adapted for use in PROVIDED 
 */

#include <stdio.h>				/* printf */
#include <stdlib.h>				/* free */
#include <string.h>				/* memcpy */
#include <asm/types.h>			/* __u64 */
#include <assert.h>
#include "ulist.h"
#include "debug.h"

int isEmptyUList(Node *head)
{
	if (head == NULL)
		return 1;
	else
		return 0;
}

/* This is like a stack, so "head" is "top" of stack */
Node *addtoUList(Node *head, void *data, int bytes) 
{
	Node *tmp = malloc(sizeof(Node));
   	memcpy(tmp->data = malloc(bytes), data, bytes);
	tmp->datasize = bytes;
   	tmp->next = head;
   	return tmp;
}

int ulistLen(Node *head) 
{
   int size = 0;
   for(; head; head = head->next, size++);
   return size;
}

/* Appends oldlist to top of newlist and returns pointer to 
 * top/head of oldlist.
 */
Node *appendUList(Node * oldlist, Node ** newlistp)
{
	Node *head = oldlist;
	int iter;
	int numitems;
	assert(*newlistp != NULL);
	if (oldlist == NULL)
	{
		head = *newlistp;
		*newlistp = NULL;
		return head;
	}

	numitems = ulistLen(oldlist);	//num items in list


   	for(iter=1; iter<=(numitems - 1); iter++)
   		oldlist = oldlist->next;

	oldlist->next = (*newlistp);

	*newlistp = NULL;
	return head;
}

/* Pop node from top of stack */
void *popUList(Node **head)
{
	Node *tmp = *head;
	void *data = malloc(tmp->datasize);
	memcpy(data, tmp->data, tmp->datasize);
	*head = (*head)->next;
	free(tmp->data);
	free(tmp);
	return data;
}

/* Stack is implemented for convenience of adding elements but in a 
 * list, index of 0 should point to 1st element, index of 1 points 
 * to next and so on. Hence, calculate appropriate number of traversal 
 * steps here.
 */
void *getIndexedNode(Node *head, int index) 
{
	int iter = ulistLen(head) - 1 - index;
//WHERE;

   	for(; iter--; head = head->next);

   	return head->data;
}

void freeUList(Node *head) 
{
   if(head->next)
      freeUList(head->next);
   free(head->data);
   free(head);
}
