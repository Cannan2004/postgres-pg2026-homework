#ifndef HALO_PMC_H
#define HALO_PMC_H
#include "postgres.h"
typedef struct HaloPmcScope HaloPmcScope;
extern char *halo_watch_pmc_list;
extern bool halo_pmc_per_tuple;
extern HaloPmcScope *HaloPmcBegin(void);
extern void HaloPmcTupleBegin(HaloPmcScope *s);
extern void HaloPmcTupleEnd(HaloPmcScope *s);
extern void HaloPmcEnd(HaloPmcScope *s);
#endif
