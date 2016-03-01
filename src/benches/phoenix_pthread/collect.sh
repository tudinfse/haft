#!/bin/bash

#==============================================================================#
# collect main *.bc files from Phoenix-pthreads builds
#==============================================================================#

set -x #echo on

PHOENIXPATH=${HOME}/bin/benchmarks/phoenix-pthreads
BENCHPATH=.

declare -a benchmarks=("histogram" "kmeans" "kmeans_nosharing" "linear_regression" "matrix_multiply" "pca" "string_match" "word_count" "word_count_nosharing")

for bm in "${benchmarks[@]}"; do
    mkdir -p ${BENCHPATH}/${bm}/obj/
    cp ${PHOENIXPATH}/${bm}/build/clang/${bm}.bc  ${BENCHPATH}/${bm}/obj/
done

