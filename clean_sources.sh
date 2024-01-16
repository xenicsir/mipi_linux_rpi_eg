#!/bin/bash

. ./environment

pushd sources
./build.sh clean ${LINUX_RPI_SRC}
popd
