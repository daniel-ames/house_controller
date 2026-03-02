# Makefile for house_controller (compliments of Sprocket)
#
# Usage:
#   make            # debug build
#   make run        # build + run
#   make release    # optimized build
#   make asan       # AddressSanitizer build (great for double-free)
#   make clean

APP := house_controller

# Prefer clang if present, else gcc
CC  := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo gcc)

# Source discovery (all .c in this directory)
SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

# Common flags
CPPFLAGS :=
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wcast-qual -Wformat=2
LDFLAGS  :=
LDLIBS   := -lpthread
CPPFLAGS += -D_POSIX_C_SOURCE=200809L

# Default target: debug build
.PHONY: all
all: debug

.PHONY: debug
debug: CFLAGS  += -O0 -g3
debug: $(APP)

.PHONY: release
release: CFLAGS += -O2 -g0 -DNDEBUG
release: $(APP)

# AddressSanitizer build: catches double-free, UAF, OOB, etc.
.PHONY: asan
asan: CFLAGS  += -O0 -g3 -fsanitize=address -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address
asan: $(APP)

# Optional: UndefinedBehaviorSanitizer too (often useful)
.PHONY: ubsan
ubsan: CFLAGS  += -O0 -g3 -fsanitize=undefined -fno-omit-frame-pointer
ubsan: LDFLAGS += -fsanitize=undefined
ubsan: $(APP)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

.PHONY: run
run: debug
	./$(APP)

.PHONY: clean
clean:
	rm -f $(APP) $(OBJS)

.PHONY: print-vars
print-vars:
	@echo "CC=$(CC)"
	@echo "SRCS=$(SRCS)"
	@echo "CFLAGS=$(CFLAGS)"