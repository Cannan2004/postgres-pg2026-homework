CC ?= gcc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -I../common
all: link_bench
link_bench: main.c link.c ../common/perf_counter.c link.h ../common/perf_counter.h
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c link.c ../common/perf_counter.c -o $@
test: link_bench
	./link_bench 10000 2000 64
sanitize:
	$(CC) $(CPPFLAGS) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer main.c link.c ../common/perf_counter.c -o link_sanitize
	./link_sanitize 1000 100 512
clean:
	rm -f link_bench link_sanitize
.PHONY: all test sanitize clean
