#!/bin/bash

# running this clones the uacpi repo, and moves the corresponding files
# to where they belong.

mkdir -p scripts_temp
cd scripts_temp

if [ -d "uACPI" ]; then
    #rm -rf ./uACPI
fi

echo "> Cloning newest uACPI version, this may include incompatible changes!"
git clone https://github.com/UltraOS/uACPI

#rm -rf ../kernel/include/uacpi
mv uACPI/include/uacpi ../kernel/include/

#rm -rf ../kernel/src/uacpi
mkdir -p ../kernel/src/uacpi
mv uACPI/source/* ../kernel/src/uacpi/