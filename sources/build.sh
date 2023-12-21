#!/bin/bash

# Build proprietary modules
for file in *; do
   if [[ -d $file && $(echo $file | cut -c1-7) != "kernel-" ]]
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

# Build native modules
file=$(echo "kernel-"$(uname -r))
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

