/* Linux perf ABI: one event per fd; measurements are current-thread/user-only.
 * mmap + RDPMC avoids a syscall for every snapshot when supported by the PMU.
 * The kernel seqlock, counter offset/width and multiplex times are all honoured.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "perf_counter.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int pc_open(PcCounter *c, uint32_t type, uint64_t config, uint64_t config1, uint64_t config2)
{
	struct perf_event_attr a;
	memset(c, 0, sizeof(*c)); c->fd = -1;
	memset(&a, 0, sizeof(a));
	a.size = sizeof(a); a.type = type; a.config = config;
	a.config1 = config1; a.config2 = config2;
	a.disabled = 1; a.exclude_kernel = 1; a.exclude_hv = 1;
	a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
	c->fd = (int)syscall(__NR_perf_event_open, &a, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
	if (c->fd < 0) { c->error = errno; return -1; }
	c->map_size = (size_t)sysconf(_SC_PAGESIZE);
	c->page = mmap(NULL, c->map_size, PROT_READ, MAP_SHARED, c->fd, 0);
	if (c->page == MAP_FAILED) c->page = NULL;
	return 0;
}
int pc_enable(PcCounter *c)
{
	if (c->fd < 0) return -1;
	if (ioctl(c->fd, PERF_EVENT_IOC_RESET, 0) || ioctl(c->fd, PERF_EVENT_IOC_ENABLE, 0)) {
		c->error = errno; return -1;
	}
	return 0;
}
int pc_disable(PcCounter *c)
{
	if (c->fd < 0) return -1;
	if (ioctl(c->fd, PERF_EVENT_IOC_DISABLE, 0)) { c->error = errno; return -1; }
	return 0;
}
#if defined(__x86_64__) || defined(__i386__)
static uint64_t ticks(void) {
	unsigned lo, hi;
	__asm__ volatile("lfence\n\trdtsc" : "=a"(lo), "=d"(hi) :: "memory");
	return ((uint64_t)hi << 32) | lo;
}
static uint64_t counter(unsigned idx) {
	unsigned lo, hi;
	__asm__ volatile("lfence\n\trdpmc\n\tlfence" : "=a"(lo), "=d"(hi) : "c"(idx) : "memory");
	return ((uint64_t)hi << 32) | lo;
}
#endif
PcSnapshot pc_read(PcCounter *c)
{
	PcSnapshot s = {0,0,0,0};
	if (c->fd < 0) return s;
#if defined(__x86_64__) || defined(__i386__)
	if (c->page) {
		struct perf_event_mmap_page *p = c->page;
		unsigned tries;
		for (tries = 0; tries < 100; ++tries) {
			uint32_t seq, idx;
			uint16_t width;
			uint64_t count, enabled, running;
			seq = __atomic_load_n(&p->lock, __ATOMIC_ACQUIRE);
			if (seq & 1) continue;
			__asm__ volatile("" ::: "memory");
			if (!p->cap_user_rdpmc) break;
			enabled = p->time_enabled; running = p->time_running;
			idx = p->index; count = (uint64_t)p->offset; width = p->pmc_width;
			if (p->cap_user_time) {
				uint64_t cyc, delta, quot, rem;
				unsigned shift;
				/* Update times even without multiplexing: short interval deltas need it. */
				cyc = ticks(); shift = p->time_shift;
				if (shift >= 64) break;
				if (p->cap_user_time_short)
					cyc = p->time_cycles + ((cyc - p->time_cycles) & p->time_mask);
				quot = cyc >> shift;
				rem = cyc & ((UINT64_C(1) << shift) - 1);
				delta = p->time_offset + quot * p->time_mult + ((rem * p->time_mult) >> shift);
				enabled += delta;
				if (idx) running += delta;
			}
			if (idx && !p->cap_user_time) break;
			if (idx && !p->cap_user_time) break;
			if (idx && !p->cap_user_time) break;
			if (idx) {
				uint64_t raw;
				if (width == 0 || width > 64) break;
				raw = counter(idx - 1);
				/* Sign-extend the hardware counter; offset accounts for wraps. */
				raw = (uint64_t)((int64_t)(raw << (64-width)) >> (64-width));
				count += raw;
			}
			__asm__ volatile("" ::: "memory");
			if (__atomic_load_n(&p->lock, __ATOMIC_ACQUIRE) != seq) continue;
			s.value=count; s.enabled=enabled; s.running=running; s.valid=1;
			++c->fast_reads; return s;
		}
	}
#endif
	{
		struct { uint64_t value, enabled, running; } v;
		ssize_t n;
		do { n = read(c->fd, &v, sizeof(v)); } while (n < 0 && errno == EINTR);
		if (n != sizeof(v)) { c->error = n < 0 ? errno : EIO; return s; }
		s.value=v.value; s.enabled=v.enabled; s.running=v.running; s.valid=1;
		++c->syscall_reads;
	}
	return s;
}
int pc_delta(PcSnapshot a, PcSnapshot b, double *scaled, double *coverage)
{
	uint64_t en, run;
	if (!a.valid || !b.valid || b.enabled < a.enabled || b.running < a.running || b.value < a.value) return -1;
	en=b.enabled-a.enabled; run=b.running-a.running;
	if (!en || !run || run > en) return -1;
	*coverage=(double)run/(double)en;
	*scaled=(double)(b.value-a.value)*(double)en/(double)run;
	return 0;
}
void pc_close(PcCounter *c)
{
	if (c->page) { munmap(c->page,c->map_size); c->page=NULL; }
	if (c->fd >= 0) { close(c->fd); c->fd=-1; }
}
