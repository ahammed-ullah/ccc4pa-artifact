CXX ?= c++
CPPFLAGS += -Isrc
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic

sources := src/main.cpp src/ccf_graph.cpp src/codec.cpp src/format.cpp src/encode.cpp
objects := $(patsubst src/%.cpp,build/%.o,$(sources))

.PHONY: all test clean
all: bin/zk_con

bin/zk_con: $(objects)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(objects) -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: bin/zk_con
	python3 tests/encode.py bin/zk_con
	python3 tests/test_summarize_times.py

clean:
	rm -rf build bin __pycache__ tests/__pycache__

-include $(objects:.o=.d)
