\set ON_ERROR_STOP on
\i course/sql/pmc_init.sql
CREATE SCHEMA IF NOT EXISTS course_lab;
SET search_path=course_lab,public;
DROP TABLE IF EXISTS r,s;
CREATE TABLE r AS SELECT i AS id, repeat('r',32) AS payload FROM generate_series(1,10000) i;
CREATE TABLE s AS SELECT i AS id, i%200 AS value, DATE '2026-01-01'+i%365 AS cdate FROM generate_series(1,10000) i;
ANALYZE r; ANALYZE s;
SET max_parallel_workers_per_gather=0;
SET enable_mergejoin=off;
SET enable_nestloop=off;
SET halo_watch_pmc_list='';
SELECT count(*) AS baseline FROM r JOIN s USING(id) WHERE s.value>100;
SET halo_watch_pmc_list='instructions,cycles,PERF_COUNT_HW_CACHE_L1D_RD,PERF_COUNT_HW_CACHE_L1D_MISS_RD';
SET halo_pmc_per_tuple=off;
EXPLAIN (ANALYZE,COSTS OFF,TIMING OFF) SELECT count(*) FROM r JOIN s USING(id) WHERE s.value>100;
SET halo_pmc_per_tuple=on;
SELECT count(*) AS instrumented FROM r JOIN s USING(id) WHERE s.value>100;
-- Repeat exercises cleanup and reopening in a single backend.
SELECT count(*) AS repeated FROM r JOIN s USING(id) WHERE s.value>100;
SET halo_watch_pmc_list='task_clock';
SELECT count(*) AS software_fallback FROM r JOIN s USING(id) WHERE s.value>100;
SET halo_watch_pmc_list='';
DROP TABLE IF EXISTS vage;
CREATE TABLE vage(id1 int,c1 varchar(30),id2 int,c2 varchar(30));
INSERT INTO vage SELECT generate_series(1,5),'AAAAAAAA'||generate_series(1,5),generate_series(1,5)+100,'aaaaaaaa'||generate_series(1,5);
CHECKPOINT;
SELECT current_setting('data_directory') AS data_directory,pg_relation_filepath('vage') AS relative_path,pg_relation_size('vage') AS bytes;
SELECT * FROM vage;
