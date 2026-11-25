#define _GNU_SOURCE
#include "link.h"
#include "perf_counter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <assert.h>
static volatile uintptr_t sink;
static void key(unsigned char *p, int size, uint64_t id) { memset(p,0xa5,(size_t)size); memcpy(p,&id,8); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static void tests(void) {
	unsigned char a[512], b[512]; int n, i;
	assert(InitLink(31,(char *)a)==NULL); assert(FindNode(NULL,32,(char *)a)==NULL);
	for(n=32;n<=512;n+=16) {
		LinkData *h,*p; key(a,n,0); h=InitLink(n,(char *)a); assert(h && h->prev==h && h->next==h);
		for(i=1;i<301;i++){ key(a,n,(uint64_t)i); errno=0; AppendNode(h,n,(char *)a); assert(!errno); }
		key(a,n,150); AppendNode(h,n,(char *)a);
		for(i=0;i<303;i++){ key(a,n,(uint64_t)i); p=FindNode(h,n,(char *)a); assert(p==FindNodeBaseline(h,n,(char *)a)); if(i<301) assert(p); else assert(!p); }
		key(a,n,150); memcpy(b,a,(size_t)n); b[n-1]^=1; assert(!FindNode(h,n,(char *)b));
		p=h; i=0; do {assert(p->next->prev==p && p->prev->next==p);++i;p=p->next;} while(p!=h);assert(i==302);
		DestroyLink(h);
	}
	puts("PASS: 32..512-byte binary keys, 302 nodes/size, duplicates, first/last/missing, ring invariants, invalid input");
}
static void bench(LinkData *h,int size,int nodes,int rounds,int optimized) {
	const char *names[]={"instructions","cycles","L1D_read_access","L1D_read_miss","L1D_write_access"};
	uint32_t types[]={0,0,3,3,3}; uint64_t cfg[]={1,0,0,65536,256};
	PcCounter c[5]; PcSnapshot a[5],b[5]; double val[5], cov[5],start,elapsed;int i,r;
	unsigned char *queries=malloc((size_t)rounds*size);
	if(!queries){perror("queries");exit(1);}
	for(r=0;r<rounds;r++) key(queries+(size_t)r*size,size,r%4==0?(uint64_t)nodes+1:(uint64_t)((r*7919ULL)%nodes));
	for(r=0;r<100;r++)sink^=(uintptr_t)FindNode(h,size,(char *)queries+(r%rounds)*size);
	for(i=0;i<5;i++){ val[i]=-1; cov[i]=0; pc_open(&c[i],types[i],cfg[i],0,0); pc_enable(&c[i]); a[i]=pc_read(&c[i]); }
	start=now();
	for(r=0;r<rounds;r++)sink^=(uintptr_t)(optimized?FindNode(h,size,(char *)queries+(size_t)r*size):FindNodeBaseline(h,size,(char *)queries+(size_t)r*size));
	elapsed=now()-start;
	for(i=0;i<5;i++){ b[i]=pc_read(&c[i]); pc_disable(&c[i]); if(pc_delta(a[i],b[i],&val[i],&cov[i]))val[i]=-1; }
	printf("\n%s nodes=%d payload=%d searches=%d elapsed_ms=%.3f ns/search=%.1f\n",optimized?"prefix+slab":"memcmp+same-slab baseline",nodes,size,rounds,elapsed*1e3,elapsed*1e9/rounds);
	for(i=0;i<5;i++) {
		if(val[i]>=0)printf("%s=%.0f coverage=%.2f%% fast_reads=%llu syscall_reads=%llu\n",names[i],val[i],cov[i]*100,(unsigned long long)c[i].fast_reads,(unsigned long long)c[i].syscall_reads);
		else printf("%s=N/A (%s)\n",names[i],c[i].error?strerror(c[i].error):"event not scheduled or invalid delta");
		pc_close(&c[i]);
	}
	if(val[2]>0 && val[3]>=0 && val[3]<=val[2])printf("L1D_read_hit_rate=%.3f%%\n",100*(1-val[3]/val[2]));
	else puts("L1D_read_hit_rate=N/A");
	if(val[2]>=0 && val[4]>=0)printf("memory_accesses_L1D_reads_plus_writes=%.0f\n",val[2]+val[4]);
	else puts("memory_accesses_L1D_reads_plus_writes=N/A (no DRAM or LLC substitution)");
	free(queries);
}
int main(int argc,char **argv) {
	int nodes=argc>1?atoi(argv[1]):10000, rounds=argc>2?atoi(argv[2]):20000,size=argc>3?atoi(argv[3]):64,i;
	LinkData *h; unsigned char buf[512]; cpu_set_t allowed,one;
	tests();
	if(nodes<1 || nodes>10000000 || rounds<1 || rounds>1000000 || size<32 || size>512)return 2;
	if(!sched_getaffinity(0,sizeof(allowed),&allowed))for(i=0;i<CPU_SETSIZE;i++)if(CPU_ISSET(i,&allowed)){CPU_ZERO(&one);CPU_SET(i,&one);if(!sched_setaffinity(0,sizeof(one),&one))printf("CPU=%d\n",i);break;}
	key(buf,size,0);h=InitLink(size,(char *)buf);if(!h)return 1;
	for(i=1;i<nodes;i++){key(buf,size,(uint64_t)i);errno=0;AppendNode(h,size,(char *)buf);if(errno){perror("AppendNode");DestroyLink(h);return 1;}}
	bench(h,size,nodes,rounds,0);bench(h,size,nodes,rounds,1);DestroyLink(h);return 0;
}
