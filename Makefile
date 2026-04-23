files := $(wildcard src/*.cpp src/*.hpp)
source_files := $(wildcard src/*.cpp)
compiler := g++
out := bin/compiler
version := c++23
libs := 

$(out): $(files)
	mkdir -p bin
	$(compiler) -time -g $(source_files) -o $(out) -std=$(version) $(libs)

.PHONY: test clean

test: $(out)
	tests/run.sh

clean:
	rm -f ./bin/*
