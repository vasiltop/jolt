files := $(wildcard src/*)
compiler := g++
out := bin/compiler
version := c++23
libs := 

$(out): $(files)
	mkdir -p bin
	$(compiler) ./src/main.cpp -o $(out) -std=$(version) $(libs)

clean:
	rm ./bin/*
