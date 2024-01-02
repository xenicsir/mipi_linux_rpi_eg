#!/bin/bash

. ./rpi_scripts.lib

pushd sources
./build.sh install ${LINUX_RPI_BUILD}
popd
