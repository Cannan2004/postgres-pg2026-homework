-- Classroom-compatible event dictionary. config2=1 means exclude kernel.
CREATE TABLE IF NOT EXISTS public.pmc_name (
 pcm_name varchar(40) PRIMARY KEY,
 pmutype integer NOT NULL, pe_config integer NOT NULL,
 config1 integer NOT NULL DEFAULT 0, config2 integer NOT NULL DEFAULT 1
);
INSERT INTO public.pmc_name VALUES
 ('cycles',0,0,0,1),('instructions',0,1,0,1),
 ('PERF_COUNT_HW_CACHE_L1D_RD',3,0,0,1),
 ('PERF_COUNT_HW_CACHE_L1D_MISS_RD',3,65536,0,1),
 ('PERF_COUNT_HW_CACHE_L1D_WR',3,256,0,1),
 ('task_clock',1,1,0,1)
ON CONFLICT (pcm_name) DO UPDATE SET pmutype=EXCLUDED.pmutype,
 pe_config=EXCLUDED.pe_config,config1=EXCLUDED.config1,config2=EXCLUDED.config2;
-- Raw events are CPU model specific; use the instructor's raw dictionary only
-- after checking the CPU event specification. Generic perf events above are
-- enough to reproduce the default experiment without guessing raw encodings.
