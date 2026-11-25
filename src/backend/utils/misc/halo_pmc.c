/* Course PMC instrumentation for PostgreSQL 16, Linux.
 * Each scope owns its fds/mappings via a MemoryContext reset callback, so
 * errors and query cancellation cannot leak descriptors. Defaults to off.
 */
#include "postgres.h"
#include "utils/halo_pmc.h"
#include "utils/perf_counter.h"
#include "utils/memutils.h"
#include "utils/builtins.h"
#include "utils/varlena.h"
#include "executor/spi.h"
#include "catalog/namespace.h"
#include "utils/lsyscache.h"
#include <errno.h>
#include <string.h>
#define HALO_PMC_MAX 8
char *halo_watch_pmc_list = NULL;
bool halo_pmc_per_tuple = false;
struct HaloPmcScope {
	MemoryContext context;
	MemoryContextCallback cleanup;
	int n;
	bool per_tuple;
	uint64 samples;
	PcCounter counters[HALO_PMC_MAX];
	PcSnapshot before[HALO_PMC_MAX];
	char names[HALO_PMC_MAX][40];
	double total[HALO_PMC_MAX], coverage[HALO_PMC_MAX];
	uint64 valid[HALO_PMC_MAX], invalid[HALO_PMC_MAX];
};
static void cleanup_pmc(void *arg) {
	HaloPmcScope *s=(HaloPmcScope *)arg;
	int i;
	for(i=0;i<s->n;i++)pc_close(&s->counters[i]);
}
static void snapshot_begin(HaloPmcScope *s) {
	int i;
	for(i=0;i<s->n;i++)s->before[i]=pc_read(&s->counters[i]);
}
static void snapshot_end(HaloPmcScope *s) {
	int i;
	for(i=0;i<s->n;i++) {
		PcSnapshot after=pc_read(&s->counters[i]);
		double value, coverage;
		if(!pc_delta(s->before[i],after,&value,&coverage)) {
			s->total[i]+=value; s->coverage[i]+=coverage; s->valid[i]++;
		} else s->invalid[i]++;
	}
	s->samples++;
}
HaloPmcScope *HaloPmcBegin(void) {
	MemoryContext ctx,old;
	HaloPmcScope *s;
	char *raw;
	List *names=NIL;
	ListCell *lc;
	uint64 row;
	if(!halo_watch_pmc_list || !halo_watch_pmc_list[0])return NULL;
	if(!OidIsValid(get_relname_relid("pmc_name",get_namespace_oid("public",true)))) {
		ereport(WARNING,(errmsg("HaloPMC: create public.pmc_name using course/sql/pmc_init.sql first")));
		return NULL;
	}
	ctx=AllocSetContextCreate(CurrentMemoryContext,"Halo PMC scope",ALLOCSET_SMALL_SIZES);
	old=MemoryContextSwitchTo(ctx);
	s=palloc0(sizeof(*s)); s->context=ctx; s->per_tuple=halo_pmc_per_tuple;
	s->cleanup.func=cleanup_pmc;s->cleanup.arg=s;
	MemoryContextRegisterResetCallback(ctx,&s->cleanup);
	raw=pstrdup(halo_watch_pmc_list);
	if(!SplitGUCList(raw,',',&names))ereport(ERROR,(errmsg("invalid halo_watch_pmc_list")));
	if(list_length(names)>HALO_PMC_MAX)ereport(ERROR,(errmsg("HaloPMC supports at most %d events",HALO_PMC_MAX)));
	if(SPI_connect()!=SPI_OK_CONNECT)elog(ERROR,"HaloPMC SPI_connect failed");
	if(SPI_execute("SELECT lower(pcm_name),pmutype,pe_config,config1,config2 FROM public.pmc_name",true,0)!=SPI_OK_SELECT)
		elog(ERROR,"HaloPMC configuration SELECT failed");
	foreach(lc,names) {
		char *requested=lfirst(lc);
		bool found=false;
		for(row=0;row<SPI_processed;row++) {
			HeapTuple tuple=SPI_tuptable->vals[row];
			TupleDesc desc=SPI_tuptable->tupdesc;
			char *name=SPI_getvalue(tuple,desc,1);
			int32 vals[4];
			bool nulls=false;
			int j,idx;
			if(!name || pg_strcasecmp(name,requested))continue;
			found=true;
			for(j=0;j<4;j++) {
				bool isnull;
				Datum d=SPI_getbinval(tuple,desc,j+2,&isnull);
				if(isnull)nulls=true;
				vals[j]=isnull?0:DatumGetInt32(d);
			}
			if(nulls || vals[0]<0 || vals[1]<0 || vals[2]<0)
				ereport(ERROR,(errmsg("HaloPMC invalid configuration for %s",requested)));
			idx=s->n++;
			strlcpy(s->names[idx],name,sizeof(s->names[idx]));
			/* The supplied table's config2 is an exclude-kernel flag, not
			 * perf_event_attr.config2. Always exclude kernel/hypervisor. */
			if(pc_open(&s->counters[idx],(uint32)vals[0],(uint64)vals[1],(uint64)vals[2],0))
				ereport(INFO,(errmsg("HaloPMC %s unavailable: %s",name,strerror(s->counters[idx].error))));
			break;
		}
		if(!found)ereport(ERROR,(errmsg("HaloPMC unknown event: %s",requested)));
	}
	SPI_finish();
	MemoryContextSwitchTo(old);
	{
		int i;
		for(i=0;i<s->n;i++)if(pc_enable(&s->counters[i]))pc_close(&s->counters[i]);
	}
	if(!s->per_tuple)snapshot_begin(s);
	return s;
}
void HaloPmcTupleBegin(HaloPmcScope *s) { if(s && s->per_tuple)snapshot_begin(s); }
void HaloPmcTupleEnd(HaloPmcScope *s) { if(s && s->per_tuple)snapshot_end(s); }
void HaloPmcEnd(HaloPmcScope *s) {
	int i;
	if(!s)return;
	if(!s->per_tuple)snapshot_end(s);
	for(i=0;i<s->n;i++)pc_disable(&s->counters[i]);
	ereport(INFO,(errmsg("HaloPMC MultiExecPrivateHash mode=%s samples=" UINT64_FORMAT,
		s->per_tuple?"per_tuple":"whole_build",s->samples)));
	for(i=0;i<s->n;i++) {
		if(s->valid[i])ereport(INFO,(errmsg("HaloPMC %s count=%.0f coverage=%.2f%% valid=" UINT64_FORMAT " invalid=" UINT64_FORMAT " rdpmc=" UINT64_FORMAT " read=" UINT64_FORMAT,
			s->names[i],s->total[i],100*s->coverage[i]/s->valid[i],s->valid[i],s->invalid[i],
			s->counters[i].fast_reads,s->counters[i].syscall_reads)));
		else ereport(INFO,(errmsg("HaloPMC %s count=N/A valid=0 invalid=" UINT64_FORMAT,s->names[i],s->invalid[i])));
	}
	MemoryContextDelete(s->context);
}
