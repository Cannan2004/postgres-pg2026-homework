\set ON_ERROR_STOP on
SET search_path=course_lab,public;
SET max_parallel_workers_per_gather=0;
SET enable_mergejoin=off;
SET enable_nestloop=off;
SET halo_watch_pmc_list='instructions';
SELECT count(*) FROM r JOIN s USING(id);
SELECT count(*) AS fds_before FROM pg_ls_dir('/proc/self/fd');
SET halo_watch_pmc_list='instructions,unknown_event_for_test';
\set ON_ERROR_STOP off
SELECT count(*) FROM r JOIN s USING(id);
SELECT count(*) FROM r JOIN s USING(id);
SELECT count(*) FROM r JOIN s USING(id);
\set ON_ERROR_STOP on
SET halo_watch_pmc_list='';
SELECT count(*) AS fds_after_errors FROM pg_ls_dir('/proc/self/fd');
SET halo_watch_pmc_list='instructions';
SET halo_pmc_per_tuple=on;
SELECT count(*) AS recovered_result FROM r JOIN s USING(id);
SET halo_watch_pmc_list='';
SELECT count(*) AS fds_after_recovery FROM pg_ls_dir('/proc/self/fd');
-- Test an unsupported PMU type; a query must still succeed and report N/A.
INSERT INTO public.pmc_name VALUES('unsupported_test',999999,0,0,1) ON CONFLICT DO NOTHING;
SET halo_watch_pmc_list='unsupported_test';
SELECT count(*) AS unavailable_event_result FROM r JOIN s USING(id);
SET halo_watch_pmc_list='';
DELETE FROM public.pmc_name WHERE pcm_name='unsupported_test';
