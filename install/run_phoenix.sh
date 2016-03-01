#!/usr/bin/env bash

# run
cd ${HAFT}src/benches/phoenix_pthread
rm -f /data/phoenix.log
./run.sh &> /data/phoenix.log

# collect
cd ${HAFT}install
./collect_phoenix.py
