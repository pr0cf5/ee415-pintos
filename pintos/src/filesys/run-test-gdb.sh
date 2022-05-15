#!/bin/sh
cd build
pintos -v -k --gdb --qemu --filesys-size=20 --swap-size=4 \
-p tests/vm/mmap-shuffle -a mmap-shuffle \
-- -q  -f run mmap-shuffle