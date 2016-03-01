#!/bin/bash

#==============================================================================#
# create inputs by making soft links of normal inputs
#==============================================================================#

set -x #echo on

PARSECPATH=${HOME}/bin/benchmarks/parsec-3.0

declare -a benchmarks=("apps/blackscholes" "apps/ferret" "apps/fluidanimate" "apps/vips" "apps/x264" "kernels/canneal" "kernels/dedup" "apps/raytrace")

for bmidx in "${!benchmarks[@]}"; do
  bm="${benchmarks[$bmidx]}"

  rm -f ${PARSECPATH}/pkgs/${bm}/inputs/input_native-native.tar
  rm -f ${PARSECPATH}/pkgs/${bm}/inputs/input_ilr-native.tar
  rm -f ${PARSECPATH}/pkgs/${bm}/inputs/input_tx-native.tar
  rm -f ${PARSECPATH}/pkgs/${bm}/inputs/input_haft-native.tar
   
  ln -f ${PARSECPATH}/pkgs/${bm}/inputs/input_native.tar ${PARSECPATH}/pkgs/${bm}/inputs/input_native-native.tar
  ln -f ${PARSECPATH}/pkgs/${bm}/inputs/input_native.tar ${PARSECPATH}/pkgs/${bm}/inputs/input_ilr-native.tar
  ln -f ${PARSECPATH}/pkgs/${bm}/inputs/input_native.tar ${PARSECPATH}/pkgs/${bm}/inputs/input_tx-native.tar
  ln -f ${PARSECPATH}/pkgs/${bm}/inputs/input_native.tar ${PARSECPATH}/pkgs/${bm}/inputs/input_haft-native.tar
done

