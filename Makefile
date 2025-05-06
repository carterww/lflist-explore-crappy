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

TARGET = lflist
SRCS = zhang.c
OBJS = $(patsubst %.c, build/%.o, $(SRCS))
LIBS = -L/usr/local/lib -l:libck.so -l:libpf.so

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): /usr/local/lib/libck.so $(BIN_DIR) $(OBJS)
	$(CC) $(C_FLAGS) $(LD_FLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD_DIR)/%.o: %.c $(BUILD_DIR)
	$(CC) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR) $(BIN_DIR):
	@mkdir $@

clean:
	@rm -rf bin build

.PHONY: all clean
