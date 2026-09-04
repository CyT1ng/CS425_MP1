#!/usr/bin/env bash
# One command, no manual intervention -- which is what the spec asks for.
#
#   ./scripts/run_tests.sh
set -euo pipefail

make clean
make all
./bin/mp1tests
