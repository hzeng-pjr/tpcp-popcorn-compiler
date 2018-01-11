#!/bin/bash

POPCORN=${1:-"/usr/local/popcorn"}
POPCORN_ARM64=$POPCORN/aarch64
export CC=$POPCORN/bin/popcorn-lib-clang
export AR=$POPCORN/bin/popcorn-ar
export TARGET="--host=aarch64-linux-gnu"
export CFLAGS="-O0 -Wno-error -popcorn-migratable"
#export CFLAGS="-target aarch64-linux-gnu"
#Other CFLAGS are set in Makefile.in so that configure execute without problem
#export LDFLAGS="-target aarch64-linux-gnu"
./configure --prefix=$POPCORN_ARM64 $TARGET --enable-static  --disable-shared
