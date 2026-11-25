#!/usr/bin/env bash
set -euo pipefail
# Run from the PostgreSQL repository root. Prefix stays task-local.
ROOT=$(pwd)
PREFIX=${PREFIX:-"$ROOT/../install"}
./configure --prefix="$PREFIX" --enable-debug --enable-cassert --without-readline --without-icu CFLAGS='-O2 -g'
make -j"${JOBS:-4}"
make install
make -C course/linked_list
cc -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -Icourse/common course/tests/test_perf.c course/common/perf_counter.c -o course/tests/test_perf
course/tests/test_perf
# Initialize a NEW isolated test cluster only; never point this at existing data.
LAB=${LAB:-"$ROOT/../lab"}
if [ -e "$LAB/data/PG_VERSION" ]; then
  echo "Existing lab found; refusing to reinitialize: $LAB" >&2; exit 1
fi
mkdir -p "$LAB/socket"
chmod 700 "$LAB/socket"
"$PREFIX/bin/initdb" -D "$LAB/data" --encoding=UTF8 --locale=C --auth-local=trust --auth-host=scram-sha-256
"$PREFIX/bin/pg_ctl" -D "$LAB/data" -l "$LAB/server.log" -o "-p 55432 -k $LAB/socket -c listen_addresses=''" start
trap '"$PREFIX/bin/pg_ctl" -D "$LAB/data" stop -m fast' EXIT
"$PREFIX/bin/psql" -h "$LAB/socket" -p 55432 -d postgres -f course/sql/demo.sql
course/linked_list/link_bench 10000 10000 64
