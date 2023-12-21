#!/bin/bash
for file in *; do
   if [[ -d $file ]]
   then
      pushd $file
      if [[ -f build.sh ]]
      then
         echo Building $file
         ./build.sh
      fi
      popd
   fi
done

