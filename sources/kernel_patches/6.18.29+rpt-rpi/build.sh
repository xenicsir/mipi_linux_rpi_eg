#!/bin/bash
for file in *; do
   if [[ -d $file ]]
   then
      pushd $file
      if [[ -f build.sh ]]
      then
         echo $1 $file
         ./build.sh $@
      fi
      popd
   fi
done

