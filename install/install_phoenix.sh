#!/usr/bin/env bash

apt-get update
apt-get install -y mercurial wget

mkdir -p /root/bin/benchmarks/
cd /root/bin/benchmarks/
hg clone https://bitbucket.org/dimakuv/phoenix-pthreads  # TODO: change to github
cd phoenix-pthreads

export HOME='/root'
make CONFIG=clang

cd ${HAFT}src/benches/phoenix_pthread
./copyinputs.sh
./collect.sh

