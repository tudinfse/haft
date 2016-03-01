#!/bin/bash

#==============================================================================#
# Run parsec benchmarks:
#   - use parsecmgt + parsecperf to run each experiment
#   - on native inputs
#==============================================================================#

#set -x #echo on

#============================== PARAMETERS ====================================#
declare -a benchmarks=("blackscholes" "ferret" "swaptions" "vips" "x264" "canneal" "streamcluster" "dedup")
benchinput="native"

declare -a threadsarr=(1 2 4 8 12 14)
declare -a typesarr=("native" "ilr" "tx" "haft")

action="parsecperftx"

#========================== EXPERIMENT SCRIPT =================================#
echo "===== Results for Parsec benchmark ====="

command -v parsecmgmt >/dev/null 2>&1 || { echo >&2 "parsecmgmt is not found (did you 'source ./env.sh'?). Aborting."; exit 1; }

# first remake all benches
for bm in "${benchmarks[@]}"; do
  for type in "${typesarr[@]}"; do
    make -C ${bm} ACTION=${type} clean
    make -C ${bm} ACTION=${type}
  done
done

# then run all benches
for times in {1..${NUM_RUNS}}; do

for bmidx in "${!benchmarks[@]}"; do
  bm="${benchmarks[$bmidx]}"

  for threads in "${threadsarr[@]}"; do
    for type in "${typesarr[@]}"; do
      echo "--- Running ${bm} ${threads} ${type} (input: ${benchinput}) ---"
      lastthreadid=$((threads-1))
      taskset -c 0-${lastthreadid} parsecmgmt -a run -p ${bm} -c clang -s "${action}" -i ${type}-${benchinput} -n ${threads}
    done  # type
  done  # threads

done  # benchmarks

done  # times
