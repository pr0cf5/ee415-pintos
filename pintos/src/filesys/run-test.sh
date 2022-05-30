#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/extended/syn-rw -a syn-rw \
-p tests/filesys/extended/child-syn-rw -a child-syn-rw \
-- -q  -f run syn-rw