#!/bin/sh
cd build
pintos -v -k --gdb --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/extended/grow-seq-lg -a grow-seq-lg \
-- -q  -f run grow-seq-lg