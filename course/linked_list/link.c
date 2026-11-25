/* Circular doubly linked list. No hashes, no index, search remains O(n).
 * Nodes and payloads are colocated in slabs to reduce malloc and cache misses.
 * A slab is allocated lazily (4 KiB) so millions of short lists do not reserve
 * large per-list arrays. Header and first payload need only one allocation.
 */
#include "link.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
#define SLAB_SIZE 4096
#define ALIGNMENT _Alignof(max_align_t)
typedef struct Slab { struct Slab *next; size_t used; max_align_t alignment; unsigned char bytes[SLAB_SIZE]; } Slab;
typedef struct Head { LinkData node; Slab *slabs; } Head;
static int valid(int n, const char *p) { return n>=32 && n<=512 && p; }
static void setup(LinkData *p, int n, char *mem, char *storage) {
	p->type=0; p->dataSize=n; p->data=storage; memcpy(p->data,mem,(size_t)n);
}
LinkData *InitLink(int size, char *mem) {
	Head *h;
	if (!valid(size,mem)) { errno=EINVAL; return NULL; }
	h=malloc(sizeof(*h)+(size_t)size); if (!h) return NULL;
	h->slabs=NULL; setup(&h->node,size,mem,(char *)(h+1));
	h->node.prev=h->node.next=&h->node; return &h->node;
}
void AppendNode(LinkData *first, int size, char *mem) {
	Head *h; Slab *s; LinkData *n; size_t bytes;
	if (!first || !valid(size,mem)) { errno=EINVAL; return; }
	h=(Head *)first; s=h->slabs;
	bytes=(sizeof(*n)+(size_t)size+ALIGNMENT-1)&~(ALIGNMENT-1);
	if (!s || s->used+bytes>SLAB_SIZE) {
		Slab *fresh=malloc(sizeof(*fresh)); if (!fresh) return;
		fresh->used=0; fresh->next=s; h->slabs=s=fresh;
	}
	n=(LinkData *)(void *)(s->bytes+s->used); s->used+=bytes;
	setup(n,size,mem,(char *)(n+1));
	n->prev=first->prev; n->next=first; first->prev->next=n; first->prev=n;
}
LinkData *FindNodeBaseline(LinkData *first, int size, char *mem) {
	LinkData *p=first;
	if (!first || !valid(size,mem)) return NULL;
	do { if (p->dataSize==size && !memcmp(p->data,mem,(size_t)size)) return p; p=p->next; } while(p!=first);
	return NULL;
}
LinkData *FindNode(LinkData *first, int size, char *mem) {
	LinkData *p=first; uint64_t prefix;
	if (!first || !valid(size,mem)) return NULL;
	memcpy(&prefix,mem,sizeof(prefix));
	do {
		LinkData *next=p->next;
		/* Exact prefix comparison (not a fingerprint/hash). memcpy permits
		 * unaligned binary keys and avoids strict-aliasing violations. */
		if (p->dataSize==size) {
			uint64_t actual; memcpy(&actual,p->data,sizeof(actual));
			if (actual==prefix && !memcmp(p->data+8,mem+8,(size_t)size-8)) return p;
		}
		p=next;
	} while(p!=first);
	return NULL;
}
void DestroyLink(LinkData *first) {
	Head *h=(Head *)first; Slab *s;
	if (!h) return;
	s=h->slabs; while(s) { Slab *next=s->next; free(s); s=next; } free(h);
}
