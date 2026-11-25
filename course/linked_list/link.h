#ifndef COURSE_LINK_H
#define COURSE_LINK_H
#include <stddef.h>
typedef struct LinkData {
	struct LinkData *prev, *next;
	int type, dataSize;
	char *data;
} LinkData;
LinkData *InitLink(int size, char *mem);
void AppendNode(LinkData *firstNode, int size, char *mem);
LinkData *FindNode(LinkData *firstNode, int size, char *mem);
LinkData *FindNodeBaseline(LinkData *firstNode, int size, char *mem);
void DestroyLink(LinkData *firstNode);
#endif
