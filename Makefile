# CS425 MP1 -- Distributed Log Querier
#
#   make            build everything into bin/ (incremental)
#   make -j8        the same, in parallel -- about 4x faster
#   make test       build and run the full unit test suite
#   make clean      throw away build/ and bin/
#
# Kept to plain make + a POSIX toolchain on purpose: the CS VM Cluster is the
# machine that has to build this, and adding a build system is one more thing
# to install at your permission level the night before the demo.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -g -Iinclude
LDFLAGS  ?= -pthread

# Have the compiler record which headers each object depends on, and read those
# records back in below. Without this, `make` only watches .cpp files: editing a
# header -- which is where every contract in this project lives -- rebuilds
# nothing, and you debug a binary that does not contain your change.
# -MP adds a dummy rule per header so a DELETED header does not wedge the build.
DEPFLAGS := -MMD -MP

BIN      := bin
OBJ      := build

LIB_SRC  := src/protocol.cpp src/net.cpp src/config.cpp \
            src/grep_runner.cpp src/client.cpp src/log_gen.cpp
LIB_OBJ  := $(LIB_SRC:%.cpp=$(OBJ)/%.o)

TEST_SRC := tests/test_main.cpp tests/harness.cpp \
            tests/test_unit_local.cpp tests/test_distributed.cpp
TEST_OBJ := $(TEST_SRC:%.cpp=$(OBJ)/%.o)

MAIN_OBJ := $(OBJ)/src/server_main.o $(OBJ)/src/client_main.o $(OBJ)/src/gen_main.o
TARGETS  := $(BIN)/log-server $(BIN)/log-query $(BIN)/log-gen $(BIN)/run-tests

.PHONY: all test clean
all: $(TARGETS)

$(BIN)/log-server: $(OBJ)/src/server_main.o $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/log-query: $(OBJ)/src/client_main.o $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/log-gen: $(OBJ)/src/gen_main.o $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/run-tests: $(TEST_OBJ) $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJ)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -Itests -c $< -o $@

# The .d files the rule above just wrote. Missing on a clean tree, which is what
# the leading '-' is for.
-include $(LIB_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(MAIN_OBJ:.o=.d)

test: $(BIN)/run-tests
	./$(BIN)/run-tests

clean:
	rm -rf $(OBJ) $(BIN)
