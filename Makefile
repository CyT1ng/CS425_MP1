# Two programs, two files, no library, no headers of our own.
#
#   make            build both
#   make clean

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread

all: server query

server: server.cpp
	g++ $(CXXFLAGS) server.cpp -o server

query: query.cpp
	g++ $(CXXFLAGS) query.cpp -o query

clean:
	rm -f server query
