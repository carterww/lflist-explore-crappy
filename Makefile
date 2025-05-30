CC = clang
BUILD_DIR = build
BIN_DIR = bin
INLCUDE = -I/usr/local/include

C_FLAGS = -std=c99 -pthread -O2 -g -fPIC -Werror -Wall -Wextra -Wpedantic -Wno-unused -Wfloat-equal \
	  -Wdouble-promotion -Wformat=2 -Wformat-security -Wstack-protector \
	  -Walloca -Wvla -Wcast-qual -Wconversion -Wformat-signedness -Wshadow \
	  -Wstrict-overflow=4 -Wundef -Wstrict-prototypes -Wswitch-default \
	  -Wswitch-enum -Wnull-dereference -Wmissing-include-dirs -Warray-bounds \
	  -Warray-bounds-pointer-arithmetic -Wassign-enum \
	  -Wbad-function-cast -Wconditional-uninitialized -Wformat-type-confusion \
	  -Widiomatic-parentheses -Wimplicit-fallthrough -Wloop-analysis \
	  -Wpointer-arith -Wshift-sign-overflow -Wshorten-64-to-32 \
	  -Wtautological-constant-in-range-compare -Wunreachable-code-aggressive \
	  -Wthread-safety -Wthread-safety-beta -Wcomma \
	  -fstack-protector-strong -fstack-clash-protection \
	  -D_FORTIFY_SOURCE=2 -fsanitize=bounds -fsanitize-undefined-trap-on-error \
	  -fsanitize=undefined -fno-omit-frame-pointer -fsanitize=safe-stack \
	  $(INCLUDE)

LD_FLAGS = -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code \
	   -Wl,-rpath /usr/local/lib

BENCH_TARGET = bench
BENCH_SRCS = bench.c lock.c zhang.c
BENCH_OBJS = $(patsubst %.c,build/%.o,$(BENCH_SRCS))

LOCK_TARGET = lock
LOCK_SRCS = lock.c
LOCK_OBJS = $(patsubst %.c, build/%.o, $(LOCK_SRCS))

HARRIS_TARGET = harris
HARRIS_SRCS = harris.c
HARRIS_OBJS = $(patsubst %.c, build/%.o, $(HARRIS_SRCS))

MICHAEL_TARGET = michael
MICHAEL_SRCS = michael.c
MICHAEL_OBJS = $(patsubst %.c, build/%.o, $(MICHAEL_SRCS))

ZHANG_TARGET = zhang
ZHANG_SRCS = zhang.c
ZHANG_OBJS = $(patsubst %.c, build/%.o, $(ZHANG_SRCS))

ZHANG2_TARGET = zhang2
ZHANG2_SRCS = zhang2.c
ZHANG2_OBJS = $(patsubst %.c, build/%.o, $(ZHANG2_SRCS))

LIBS = -L/usr/local/lib -l:libck.so -l:libpf.so

all: bench

bench: $(BIN_DIR)/$(BENCH_TARGET)

lock: $(BIN_DIR)/$(LOCK_TARGET)

harris: $(BIN_DIR)/$(HARRIS_TARGET)

michael: $(BIN_DIR)/$(MICHAEL_TARGET)

zhang: $(BIN_DIR)/$(ZHANG_TARGET)

zhang2: $(BIN_DIR)/$(ZHANG2_TARGET)

$(BIN_DIR)/$(BENCH_TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(BENCH_OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(BENCH_OBJS) $(LIBS) -o $@

$(BIN_DIR)/$(LOCK_TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(LOCK_OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(LOCK_OBJS) $(LIBS) -o $@

$(BIN_DIR)/$(HARRIS_TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(HARRIS_OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(HARRIS_OBJS) $(LIBS) -o $@

$(BIN_DIR)/$(MICHAEL_TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(MICHAEL_OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(MICHAEL_OBJS) $(LIBS) -o $@

$(BIN_DIR)/$(ZHANG_TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(ZHANG_OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(ZHANG_OBJS) $(LIBS) -o $@

$(BIN_DIR)/$(ZHANG2_TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(ZHANG2_OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(ZHANG2_OBJS) $(LIBS) -o $@

$(BUILD_DIR)/%.o: %.c $(BUILD_DIR)
	$(CC) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR) $(BIN_DIR):
	@mkdir $@

clean:
	@rm -rf bin build

.PHONY: all bench lock harris michael zhang zhang2 clean
