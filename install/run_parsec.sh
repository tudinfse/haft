#!/usr/bin/env bash

# run
cd /root/bin/benchmarks/parsec-3.0
source env.sh

cd ${HAFT}src/benches/parsec
rm -f /data/parsec.log
./run.sh &> /data/parsec.log

# collect
cd ${HAFT}install
./collect_parsec.py
