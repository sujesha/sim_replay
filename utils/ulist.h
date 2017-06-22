#ifndef _ULIST_H_
#define _ULIST_H_

#ifndef CONFIG_X86_64 
    //32-bit
    #define PRI_SIZEOF "%u"
#else
    //64-bit
    #define PRI_SIZEOF "%lu"
#endif

typedef struct Node{
    void *data;
    int datasize;
    struct Node *next;
}Node;



int isEmptyUList(Node *head);
Node *addtoUList(Node *head, void *data, int bytes);
int ulistLen(Node *head);
Node *appendUList(Node * oldlist, Node ** newlistp);
void *popUList(Node **head);
void *getIndexedNode(Node *head, int index);
void freeUList(Node *head);

#endif /* _ULIST_H_ */
