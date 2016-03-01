#!/bin/bash

rm -rf tmp/

### Phoenix
./run.sh histogram
./run.sh kmeans
./run.sh linear_regression
./run.sh matrix_multiply
./run.sh pca
./run.sh string_match
./run.sh word_count

### PARSEC
./run.sh blackscholes
./run.sh canneal
./run.sh dedup
./run.sh ferret
./run.sh streamcluster
./run.sh swaptions
#./run.sh vips  ## FIXME: non-deterministic
./run.sh x264

### Memcached -- event-based
#./run.sh memcached

### other case-studies
#./run.sh leveldb
#./run.sh sqlite3

