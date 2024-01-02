#!/bin/bash

. ./rpi_scripts.lib

pushd sources
./build.sh clean ${LINUX_RPI_BUILD}
popd
