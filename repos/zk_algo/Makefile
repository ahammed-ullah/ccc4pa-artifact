CXX ?= c++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -O3 -DNDEBUG
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -MMD -MP

SRC := $(wildcard src/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))

.PHONY: all test clean

all: bin/zk_algo

bin/zk_algo: $(OBJ) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/zk_check: tests/check_ccf.cpp src/graph.cpp ../zk_con/src/ccf_graph.cpp | bin
	$(CXX) $(CPPFLAGS) -I../zk_con/src $(CXXFLAGS) $^ -o $@

bin/zk_mvm_check: tests/check_mvm.cpp src/graph.cpp src/mvm.cpp src/weights.cpp | bin
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

bin:
	mkdir -p bin

test: bin/zk_algo bin/zk_check bin/zk_mvm_check
	python3 tests/integration.py

clean:
	rm -rf build bin tests/__pycache__

-include $(OBJ:.o=.d)
