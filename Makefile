files := $(wildcard src/*.cpp src/*.hpp)
compiler := g++
out := bin/compiler
version := c++23
libs := 

$(out): $(files)
	mkdir -p bin
	$(compiler) src/*.cpp -o $(out) -std=$(version) $(libs)

clean:
	rm -f ./bin/*
