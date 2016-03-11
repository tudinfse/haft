# Dockerfile for HAFT

FROM ubuntu

MAINTAINER Dmitrii Kuvaiskii (dmitrii.kuvaiskii@tu-dresden.de)

# == Basic packages ==
RUN apt-get update && \
    apt-get install -y git \
                       texinfo \
                       vim \
                       libxml2-dev \
                       cmake \
                       python \
                       gcc \
                       build-essential \
                       flex \
                       bison \
                       linux-tools-generic

# use bash not sh
RUN rm /bin/sh && \
    ln -s /bin/bash /bin/sh

# get correct perf
RUN list=( /usr/lib/linux-tools/*-generic/perf ) && \
    ln -sf ${list[-1]} /usr/bin/perf

# == LLVM & CLang ==
# prepare environment
ENV LLVM_SOURCE=/root/bin/llvm/llvm/ \
    LLVM_BUILD=/root/bin/llvm/build/ \
    CLANG_SOURCE=/root/bin/llvm/llvm/tools/clang/ \
    GOLD_PLUGIN=/root/bin/binutils/

RUN mkdir -p $LLVM_SOURCE $LLVM_BUILD $GOLD_PLUGIN ${GOLD_PLUGIN}build

# get correct versions of sources
RUN git clone https://github.com/llvm-mirror/llvm $LLVM_SOURCE && \
    git clone --depth 1 git://sourceware.org/git/binutils-gdb.git ${GOLD_PLUGIN}binutils && \
    git clone http://llvm.org/git/compiler-rt.git ${LLVM_SOURCE}projects\compiler-rt && \
    git clone http://llvm.org/git/openmp.git ${LLVM_SOURCE}projects\openmp && \
    git clone https://github.com/llvm-mirror/clang $CLANG_SOURCE

WORKDIR $LLVM_SOURCE
COPY install/patches/llvm370-01-x86-ilr-nocmp.patch ./
COPY install/patches/llvm370-02-x86-xabort.patch ./
RUN git checkout 509fb2c84c5b1cbff85c5963d5a112dd157e91ad && \
    git apply llvm370-01-x86-ilr-nocmp.patch && \
    git apply llvm370-02-x86-xabort.patch

WORKDIR $CLANG_SOURCE
RUN git checkout e7b486824bfac07b13bb554edab7d62452dab4d8

# build
WORKDIR $LLVM_BUILD
RUN cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="Release" -DLLVM_TARGETS_TO_BUILD="X86" -DCMAKE_INSTALL_PREFIX=${LLVM_BUILD} -DLLVM_BINUTILS_INCDIR=${GOLD_PLUGIN}binutils/include ../llvm && \
    make -j8 && \
    make install

# == Gold Linker ==
WORKDIR ${GOLD_PLUGIN}build
RUN ../binutils/configure --enable-gold --enable-plugins --disable-werror
RUN make all-gold && \
    make

RUN cp gold/ld-new /usr/bin/ld && \
    cp binutils/ar /usr/bin/ar && \
    cp binutils/nm-new /usr/bin/nm-new && \
    \
    mkdir -p /usr/lib/bfd-plugins && \
    cp ${LLVM_BUILD}/lib/LLVMgold.so /usr/lib/bfd-plugins

# == HAFT ==
ENV HAFT=/root/code/haft/
COPY ./ ${HAFT}

RUN make -C ${HAFT}src/tx/pass && \
    make -C ${HAFT}src/tx/runtime && \
    make -C ${HAFT}src/ilr/pass && \
    \
    make -C ${HAFT}src/benches/util/libc ACTION=helper && \
    make -C ${HAFT}src/benches/util/renamer

VOLUME /data

WORKDIR /root/code/haft/

# == Environment variables ==
# number of runs in each performance experiment
ENV NUM_RUNS=1

# == Interface ==
ENTRYPOINT ["/bin/bash"]
