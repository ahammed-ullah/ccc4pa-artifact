CXX ?= c++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -O3 -DNDEBUG
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -MMD -MP

MAIN_SRC := src/main.cpp
SRC := $(filter-out $(MAIN_SRC),$(wildcard src/*.cpp))
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))
MAIN_OBJ := build/main.o
TEST_OBJ := build/test_main.o

.PHONY: all test unit-test integration-test clean

all: bin/fm_con

bin/fm_con: $(OBJ) $(MAIN_OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

build/fm_con_tests: $(OBJ) $(TEST_OBJ) | build
	$(CXX) $(CXXFLAGS) $^ -o $@

unit-test: build/fm_con_tests
	./build/fm_con_tests

integration-test: unit-test bin/fm_con
	bash tests/integration.sh

test: integration-test

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/test_main.o: tests/test_main.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build bin:
	mkdir -p $@

clean:
	rm -rf build bin

-include $(OBJ:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

