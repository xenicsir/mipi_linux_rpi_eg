#!/bin/bash
for file in *; do
   if [[ -d $file ]]
   then
      pushd $file
      if [[ -f install.sh ]]
      then
         echo Installing $file
         ./install.sh
      fi
      popd
   fi
done

