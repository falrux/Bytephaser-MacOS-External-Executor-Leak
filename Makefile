CXX ?= clang++
CC ?= clang

TARGET := external
ARCH ?= $(shell uname -m)

DEPS_DIR := dependencies

LUAU_SRCS := \
	$(DEPS_DIR)/Luau/Ast.cpp \
	$(DEPS_DIR)/Luau/BuiltinFolding.cpp \
	$(DEPS_DIR)/Luau/Builtins.cpp \
	$(DEPS_DIR)/Luau/BytecodeBuilder.cpp \
	$(DEPS_DIR)/Luau/Compiler.cpp \
	$(DEPS_DIR)/Luau/Confusables.cpp \
	$(DEPS_DIR)/Luau/ConstantFolding.cpp \
	$(DEPS_DIR)/Luau/CostModel.cpp \
	$(DEPS_DIR)/Luau/Lexer.cpp \
	$(DEPS_DIR)/Luau/Location.cpp \
	$(DEPS_DIR)/Luau/Parser.cpp \
	$(DEPS_DIR)/Luau/StringUtils.cpp \
	$(DEPS_DIR)/Luau/TableShape.cpp \
	$(DEPS_DIR)/Luau/TimeTrace.cpp \
	$(DEPS_DIR)/Luau/Types.cpp \
	$(DEPS_DIR)/Luau/ValueTracking.cpp

BLAKE3_SRCS := \
	$(DEPS_DIR)/blake3/blake3.c \
	$(DEPS_DIR)/blake3/blake3_dispatch.c \
	$(DEPS_DIR)/blake3/blake3_portable.c

CPP_SRCS := external_executor.cpp $(LUAU_SRCS)
C_SRCS := $(BLAKE3_SRCS)

COMMON_DEFS := -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 -DBLAKE3_USE_NEON=0
COMMON_INC := -I. -I$(DEPS_DIR)/Luau -I$(DEPS_DIR)/blake3
COMMON_CXXFLAGS := -std=c++20 -Wall -Wextra -arch $(ARCH) $(COMMON_INC) $(COMMON_DEFS) -O2
COMMON_CFLAGS := -Wall -Wextra -arch $(ARCH) $(COMMON_INC) $(COMMON_DEFS) -O2
COMMON_LDFLAGS := -arch $(ARCH) -framework CoreGraphics -framework CoreFoundation

ZSTD_LDFLAGS :=
ifeq ($(wildcard /opt/homebrew/lib/libzstd.dylib),/opt/homebrew/lib/libzstd.dylib)
	ZSTD_LDFLAGS += -L/opt/homebrew/lib
endif

OBJS := $(CPP_SRCS:.cpp=.o) $(C_SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(COMMON_LDFLAGS) $(ZSTD_LDFLAGS) -o $@ $(OBJS) -lzstd

%.o: %.cpp
	$(CXX) $(COMMON_CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean