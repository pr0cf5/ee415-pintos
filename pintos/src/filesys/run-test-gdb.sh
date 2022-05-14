#!/bin/sh
cd build
pintos -v -k --gdb --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/base/child-syn-read -a child-syn-read \
-p tests/filesys/base/syn-read -a syn-read \
-- -q  -f run syn-read
