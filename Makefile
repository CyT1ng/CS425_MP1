# CS425 MP1 -- Distributed Log Querier
#
#   make            build everything into bin/
#   make test       build and run the full unit test suite
#   make clean
#
# Kept to plain make + a POSIX toolchain on purpose: the CS VM Cluster is the
# machine that has to build this, and adding a build system is one more thing
# to install at your permission level the night before the demo.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -g -Iinclude
LDFLAGS  ?= -pthread

BIN      := bin
OBJ      := build

LIB_SRC  := src/protocol.cpp src/net.cpp src/config.cpp \
            src/grep_runner.cpp src/client.cpp src/log_gen.cpp
LIB_OBJ  := $(LIB_SRC:%.cpp=$(OBJ)/%.o)

TEST_SRC := tests/test_main.cpp tests/harness.cpp \
            tests/test_unit_local.cpp tests/test_distributed.cpp
TEST_OBJ := $(TEST_SRC:%.cpp=$(OBJ)/%.o)

TARGETS  := $(BIN)/mp1d $(BIN)/dgrep $(BIN)/mp1gen $(BIN)/mp1tests

.PHONY: all test clean
all: $(TARGETS)

$(BIN)/mp1d: $(OBJ)/src/server_main.o $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/dgrep: $(OBJ)/src/client_main.o $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/mp1gen: $(OBJ)/src/gen_main.o $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/mp1tests: $(TEST_OBJ) $(LIB_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJ)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Itests -c $< -o $@

test: $(BIN)/mp1tests
	./$(BIN)/mp1tests

clean:
	rm -rf $(OBJ) $(BIN)
