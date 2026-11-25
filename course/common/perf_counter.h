#ifndef COURSE_PERF_COUNTER_H
#define COURSE_PERF_COUNTER_H
#include <stdint.h>
#include <stddef.h>
#include <linux/perf_event.h>

typedef struct PcCounter {
	int fd, error;
	size_t map_size;
	struct perf_event_mmap_page *page;
	uint64_t fast_reads, syscall_reads;
} PcCounter;
typedef struct PcSnapshot {
	uint64_t value, enabled, running;
	int valid;
} PcSnapshot;
int pc_open(PcCounter *c, uint32_t type, uint64_t config, uint64_t config1, uint64_t config2);
int pc_enable(PcCounter *c);
int pc_disable(PcCounter *c);
PcSnapshot pc_read(PcCounter *c);
int pc_delta(PcSnapshot a, PcSnapshot b, double *scaled, double *coverage);
void pc_close(PcCounter *c);
#endif
