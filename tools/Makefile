# Makefile for NPU debugging tools
# Compile on the target board (ROCK 3B)

CC = gcc
CFLAGS = -Wall -O2

TOOLS = check_pmu_bsp test_npu_reg test_open test_hw_version

all: $(TOOLS)

check_pmu_bsp: check_pmu_bsp.c
	$(CC) $(CFLAGS) -o $@ $<

test_npu_reg: test_npu_reg.c
	$(CC) $(CFLAGS) -o $@ $<

test_open: test_open.c
	$(CC) $(CFLAGS) -o $@ $<

test_hw_version: test_hw_version.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TOOLS)

install: all
	mkdir -p ~/npu-tools
	cp $(TOOLS) *.c Makefile ~/npu-tools/

.PHONY: all clean install
