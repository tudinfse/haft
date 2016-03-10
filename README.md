# HAFT (Hardware Assisted Fault Tolerance)

HAFT is a compiler framework that transforms unmodified multithreaded applications to support fault detection via instruction-level replication (ILR) and fault recovery via hardware transactional memory (HTM, in our case Intel TSX). See [HAFT paper](link) for details.

## Benchmarks

This repository provides two benchmark suites, PARSEC and Phoenix (pthreads version):

* [PARSEC 3.0](http://parsec.cs.princeton.edu)

* [Phoenix-pthreads 2.0](https://bitbucket.org/dimakuv/phoenix-pthreads)

The other benchmarks (LogCabin, Memcached, SQLite3, LevelDB, and Apache) are *not* found in this repository. Please ask us directly if you need them via email:
`dmitrii.kuvaiskii \[at\] tu-dresden \[dot\] de`.

## Docker

Docker Hub contains a ready-to-use [Docker image](https://hub.docker.com/r/tudinfse/haft/).

## Installation and Performance Experiments

* You can always use the ready-to-use docker image. However, you can also build a new docker image locally:

```sh
make build   # creates haft_container docker
```

* To run the docker image, use:

```sh
make run   # runs haft_container docker
```

* In docker, to install benchmarks:

```sh
./install/install_parsec.sh
./install/install_phoenix.sh
```

* In docker, to run benchmarks: 

```sh
EXPORT NUM_RUNS=10   # by default, each benchmark is run once
./install/run_parsec.sh
./install/run_phoenix.sh
```

* The results of benchmark runs are aggregated in two logs, saved in your current directory under data/:

```sh
less data/parsec.log       # complete log of PARSEC benchmarks' runs
less data/parsec_raw.txt   # aggregated results of PARSEC benchmarks' runs
less data/phoenix.log      # complete log of Phoenix benchmarks' runs
less data/phoenix_raw.txt  # aggregated results of Phoenix benchmarks' runs
```
