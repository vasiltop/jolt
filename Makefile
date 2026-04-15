files := $(wildcard src/*.cpp)
compiler := g++
out := bin/compiler
version := c++23
libs := 

$(out): $(files)
	mkdir bin -p
	$(compiler) ./src/main.cpp -o $(out) -std=$(version) $(libs)

clean:
	rm ./bin/*
