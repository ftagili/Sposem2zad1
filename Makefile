CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
TARGET := spo_task1_v18
ARCH ?= $(shell uname -m)

ifeq ($(OS),Windows_NT)
THREAD_FLAGS :=
else
THREAD_FLAGS :=
endif

ifeq ($(ARCH),s390x)
SOURCES := scheduler.c workload_s390x.S
else
SOURCES := scheduler.c workload_portable.c
endif

all: $(TARGET)

$(TARGET): $(SOURCES) workload.h
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(SOURCES) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

self-test: $(TARGET)
	./$(TARGET) --self-test

clean:
	rm -f $(TARGET)

.PHONY: all run self-test clean
