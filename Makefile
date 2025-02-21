CXX := g++
CXXFLAGS := -std=c++17

LLVM_INCLUDE := $(shell llvm-config --includedir)
LLVM_LIBS := $(shell llvm-config --libs)
LLVM_LDFLAGS := $(shell llvm-config --ldflags)
LLVM_SYSTEM_LIBS := $(shell llvm-config --system-libs)

SRC := src/*.cpp
GEN := bin/generator
IR_TARGET := bin/compiler.ll
TARGET := bin/compiler

$(shell mkdir -p bin)

run:
	$(CXX) $(CXXFLAGS) -g -I$(LLVM_INCLUDE) $(SRC) -o $(GEN) $(LLVM_LIBS) $(LLVM_LDFLAGS) $(LLVM_SYSTEM_LIBS) -fsanitize=address
	./$(GEN) examples/main.pl

compile-ir:
	./$(GEN) > $(IR_TARGET)
	clang -o $(TARGET) $(IR_TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

