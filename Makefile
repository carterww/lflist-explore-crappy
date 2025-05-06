CC = clang
BUILD_DIR = build
BIN_DIR = bin
INLCUDE = -I/usr/local/include

C_FLAGS = -std=c99 -pthread -O2 -fPIC -Werror -Wall -Wextra -Wpedantic -Wno-unused -Wfloat-equal \
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

ZHANG_TARGET = zhang
ZHANG_SRCS = zhang.c
ZHANG_OBJS = $(patsubst %.c, build/%.o, $(ZHANG_SRCS))

ZHANG2_TARGET = zhang2
ZHANG2_SRCS = zhang2.c
ZHANG2_OBJS = $(patsubst %.c, build/%.o, $(ZHANG2_SRCS))

LIBS = -L/usr/local/lib -l:libck.so -l:libpf.so

all: zhang2

zhang: $(BIN_DIR)/$(ZHANG_TARGET)

zhang2: $(BIN_DIR)/$(ZHANG2_TARGET)

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

.PHONY: all zhang zhang2 clean
