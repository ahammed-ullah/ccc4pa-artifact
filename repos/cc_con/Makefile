CXX ?= c++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -O3 -DNDEBUG
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -MMD -MP

MAIN_SRC := src/main.cpp src/cg_prepare_main.cpp src/cc_check_main.cpp \
	src/cc_bound_main.cpp
BOUND_SRC := src/bound.cpp src/bound_shared.cpp src/bound_linear.cpp \
	src/bound_local_mis.cpp src/bound_edge_color.cpp src/bound_truss.cpp \
	src/bound_report.cpp
SRC := $(filter-out $(MAIN_SRC) $(BOUND_SRC),$(wildcard src/*.cpp))
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))
BOUND_OBJ := $(patsubst src/%.cpp,build/%.o,$(BOUND_SRC))
CLI_OBJ := build/main.o
CG_PREPARE_OBJ := build/cg_prepare_main.o
CC_CHECK_OBJ := build/cc_check_main.o
CC_BOUND_OBJ := build/cc_bound_main.o
TEST_OBJ := build/test_main.o
BOUND_TEST_OBJ := build/bound_test.o

GRAPH_SRC := src/artifact_support.cpp src/cgf.cpp src/config.cpp src/graph.cpp \
	src/sx_tag_question_support.cpp \
	$(wildcard src/graph_*.cpp)
GRAPH_OBJ := $(patsubst src/%.cpp,build/%.o,$(GRAPH_SRC))
CC_CHECK_SUPPORT_OBJ := build/output.o build/verification.o

.PHONY: all test unit-test integration-test clean

all: bin/cc_con bin/cg_prepare bin/cc_check bin/cc_bound

bin/cc_con: $(OBJ) $(CLI_OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/cg_prepare: $(GRAPH_OBJ) $(CG_PREPARE_OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/cc_check: $(GRAPH_OBJ) $(CC_CHECK_SUPPORT_OBJ) $(CC_CHECK_OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/cc_bound: $(GRAPH_OBJ) $(BOUND_OBJ) $(CC_BOUND_OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

build/cc_con_tests: $(OBJ) $(TEST_OBJ) | build
	$(CXX) $(CXXFLAGS) $^ -o $@

build/cc_bound_tests: $(GRAPH_OBJ) $(BOUND_OBJ) $(BOUND_TEST_OBJ) | build
	$(CXX) $(CXXFLAGS) $^ -o $@

unit-test: build/cc_con_tests build/cc_bound_tests
	./build/cc_con_tests
	./build/cc_bound_tests

integration-test: unit-test bin/cc_con bin/cg_prepare bin/cc_check bin/cc_bound
	bash tests/integration.sh

test: integration-test

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/test_main.o: tests/test_main.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/bound_test.o: tests/bound_test.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build bin:
	mkdir -p $@

clean:
	rm -rf build bin

-include $(OBJ:.o=.d) $(BOUND_OBJ:.o=.d) $(CLI_OBJ:.o=.d) \
	$(CG_PREPARE_OBJ:.o=.d) $(CC_CHECK_OBJ:.o=.d) $(CC_BOUND_OBJ:.o=.d) \
	$(TEST_OBJ:.o=.d) $(BOUND_TEST_OBJ:.o=.d)
