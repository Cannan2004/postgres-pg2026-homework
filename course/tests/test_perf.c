#include "perf_counter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
	PcSnapshot a={10,100,50,1}, b={30,200,100,1};
	double value,coverage; PcCounter c; PcSnapshot x,y;
	volatile unsigned long sum=0; unsigned i;
	assert(!pc_delta(a,b,&value,&coverage));assert(value==40 && coverage==0.5);
	b.running=a.running;assert(pc_delta(a,b,&value,&coverage));
	b=a;b.valid=0;assert(pc_delta(a,b,&value,&coverage));
	assert(pc_open(&c,999999,0,0,0)<0);assert(!pc_read(&c).valid);pc_close(&c);pc_close(&c);
	if(!pc_open(&c,PERF_TYPE_SOFTWARE,PERF_COUNT_SW_TASK_CLOCK,0,0)) {
		assert(!pc_enable(&c));x=pc_read(&c);for(i=0;i<1000000;i++)sum+=i;y=pc_read(&c);
		assert(!pc_delta(x,y,&value,&coverage));assert(value>0);assert(c.syscall_reads==2);
		assert(!pc_disable(&c));pc_close(&c);
	} else puts("SKIP software event: unavailable");
#if defined(__x86_64__) || defined(__i386__)
	/* ABI metadata test only: index=0 must never execute RDPMC. This does
	 * NOT claim that a hardware RDPMC instruction was exercised. */
	memset(&c,0,sizeof(c));c.fd=123456;c.map_size=(size_t)sysconf(_SC_PAGESIZE);
	c.page=mmap(NULL,c.map_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
	assert(c.page!=MAP_FAILED);c.page->cap_user_rdpmc=1;c.page->offset=42;
	c.page->time_enabled=100;c.page->time_running=100;
	x=pc_read(&c);assert(x.valid && x.value==42 && c.fast_reads==1 && c.syscall_reads==0);
	munmap(c.page,c.map_size);
#endif
	printf("PASS: delta scaling, zero-time rejection, invalid-event cleanup, software read fallback, mmap index-zero metadata; checksum=%lu\n",sum);
	return 0;
}
