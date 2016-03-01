#!/bin/bash

#==============================================================================#
# collect main *.opt.bc files from Parsec builds
#==============================================================================#

set -x #echo on

PARSECPATH=${HOME}/bin/benchmarks/parsec-3.0
LLVMBENCHPATH=.
CONFIG=amd64-linux.clang

mkdir -p ${LLVMBENCHPATH}/blackscholes/obj/
mkdir -p ${LLVMBENCHPATH}/ferret/obj/
mkdir -p ${LLVMBENCHPATH}/swaptions/obj/
mkdir -p ${LLVMBENCHPATH}/vips/obj/
mkdir -p ${LLVMBENCHPATH}/x264/obj/
mkdir -p ${LLVMBENCHPATH}/canneal/obj/
mkdir -p ${LLVMBENCHPATH}/streamcluster/obj/
mkdir -p ${LLVMBENCHPATH}/dedup/obj/

cp ${PARSECPATH}/pkgs/apps/blackscholes/obj/${CONFIG}/blackscholes.opt.bc ${LLVMBENCHPATH}/blackscholes/obj/

cp ${PARSECPATH}/pkgs/apps/ferret/obj/${CONFIG}/parsec/bin/ferret-pthreads.opt.bc ${LLVMBENCHPATH}/ferret/obj/

cp ${PARSECPATH}/pkgs/apps/swaptions/obj/${CONFIG}/swaptions.opt.bc ${LLVMBENCHPATH}/swaptions/obj/

cp ${PARSECPATH}/pkgs/apps/vips/obj/${CONFIG}/tools/iofuncs/vips.opt.bc ${LLVMBENCHPATH}/vips/obj/

cp ${PARSECPATH}/pkgs/apps/x264/obj/${CONFIG}/x264.opt.bc ${LLVMBENCHPATH}/x264/obj/

cp ${PARSECPATH}/pkgs/kernels/canneal/obj/${CONFIG}/canneal.opt.bc ${LLVMBENCHPATH}/canneal/obj/

cp ${PARSECPATH}/pkgs/kernels/streamcluster/obj/${CONFIG}/streamcluster.opt.bc ${LLVMBENCHPATH}/streamcluster/obj/

cp ${PARSECPATH}/pkgs/kernels/dedup/obj/${CONFIG}/dedup.opt.bc ${LLVMBENCHPATH}/dedup/obj/

