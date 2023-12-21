#!/bin/bash

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Build on target (not on host)
   make -C /lib/modules/$(uname -r)/build M=$PWD
fi

