CXX ?= c++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -O3 -DNDEBUG
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -MMD -MP

SRC := $(wildcard src/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))

.PHONY: all test test-mvm clean

all: bin/wg_algo

bin/wg_algo: $(OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

bin:
	mkdir -p bin

test-mvm: build/test_mvm
	./build/test_mvm

build/test_mvm: src/mvm.cpp tests/mvm_oracle.cpp tests/mvm_stub/wg_algo/algorithms.hpp
	@mkdir -p build
	$(CXX) -Itests/mvm_stub $(CXXFLAGS) -UNDEBUG src/mvm.cpp tests/mvm_oracle.cpp -o $@

test: bin/wg_algo test-mvm
	python3 tests/integration.py

clean:
	rm -rf build bin tests/__pycache__

-include $(OBJ:.o=.d)
