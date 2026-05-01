files := $(wildcard src/*.cpp src/*.hpp)
source_files := $(wildcard src/*.cpp)
compiler := g++
out := bin/jolt
version := c++23

LLVM_CONFIG ?= llvm-config
LLVM_CPPFLAGS := $(shell $(LLVM_CONFIG) --cppflags 2>/dev/null)
LLVM_CXXFLAGS_EXTRA := $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null)
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs core 2>/dev/null)

libs := $(LLVM_LDFLAGS) $(LLVM_LIBS)

# llvm-config emits -O/-std; enforce C++23 last on the compile line.
cxxflags_extra := $(filter-out -O% -std=c++17 -std=gnu++17 -std=c++14 -std=gnu++14,\
	$(LLVM_CPPFLAGS) $(LLVM_CXXFLAGS_EXTRA))

$(out): $(files)
	mkdir -p bin
	@if ! $(LLVM_CONFIG) --version >/dev/null 2>&1; then \
		echo "error: LLVM not found. Install LLVM development packages and ensure '$(LLVM_CONFIG)' is on PATH." >&2; \
		exit 1; \
	fi
	$(compiler) -g $(source_files) $(cxxflags_extra) -std=$(version) -o $(out) $(libs)

.PHONY: test clean

test: $(out)
	tests/run.sh

clean:
	rm -rf bin/jolt bin/jolt.dSYM
