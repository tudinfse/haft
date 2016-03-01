#!/bin/bash

#==============================================================================#
# Run Phoenix benchmarks:
#   - on large inputs
#   - use taskset -c 0-.. to limit number of CPUs
#==============================================================================#

#set -x #echo on

#============================== PARAMETERS ====================================#
MATRIXMULSIZE=1500

declare -a benchmarks=( \
"histogram" \
"kmeans" \
"kmeans_nosharing" \
"linear_regression" \
"matrix_multiply" \
"pca" \
"string_match" \
"word_count" \
"word_count_nosharing" \
)

declare -a benchinputs=(\
"input/large.bmp"\                 # histogram
" "\                               # kmeans -- dont need anything
" "\                               # kmeans_nosharing -- dont need anything
"input/key_file_500MB.txt"\        # linear
"$MATRIXMULSIZE"\                  # matrix multiply (requires created files)
"-r 3000 -c 3000"\                 # pca
"input/key_file_500MB.txt"\        # string match
"input/word_100MB.txt"\            # word count
"../word_count/input/word_100MB.txt"\            # take input from word count
)

declare -a threadsarr=(1 2 4 8 12 14)
declare -a typesarr=("native" "ilr" "tx" "haft")

#action="perf stat -e cpu/cpu-cycles/,cpu/cycles-t/,cpu/cycles-ct/"
action="perf stat -e cycles,instructions -e tx-start,tx-commit,tx-abort -e tx-capacity,tx-conflict"

#========================== EXPERIMENT SCRIPT =================================#
echo "===== Results for Phoenix benchmark ====="

# first remake all benches
for bm in "${benchmarks[@]}"; do
  for type in "${typesarr[@]}"; do
    make -C ${bm} ACTION=${type} clean
    make -C ${bm} ACTION=${type}
  done
done

# special case of matrix_multiply: need to create files
cd ./matrix_multiply
./matrix_multiply.native.exe $MATRIXMULSIZE 1 > /dev/null 2>&1
cd ..

# then run all benches
for times in {1..${NUM_RUNS}}; do

for bmidx in "${!benchmarks[@]}"; do
  bm="${benchmarks[$bmidx]}"
  in="${benchinputs[$bmidx]}"

  cd ./${bm}

  # dry run to load files into RAM
  ./${bm}.native.exe ${in} > /dev/null 2>&1

  for threads in "${threadsarr[@]}"; do
    for type in "${typesarr[@]}"; do

      echo "--- Running ${bm} ${threads} ${type} (input: '${in}') ---"
      lastthreadid=$((threads-1))
      ${action} taskset -c 0-${lastthreadid} ./${bm}.${type}.exe ${in}

    done  # type
  done  # threads

  cd ../
done # benchmarks

done # times
