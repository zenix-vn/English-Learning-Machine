#!/bin/sh
# Builds and runs the host tests for the offline-study core. Needs g++ (use any Linux box or: docker run alpine).
set -eu
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror ${SAN--fsanitize=address,undefined} \
    ../../main/offline/sm2.cc ../../main/offline/pack.cc ../../main/offline/progress.cc ../../main/offline/card_queue.cc ../../main/offline/text_util.cc \
    offline_test.cpp -o offline_test
./offline_test fixtures/pack60 fixtures/sm2_cases.txt
