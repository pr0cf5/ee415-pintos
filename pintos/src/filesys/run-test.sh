#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/base/child-syn-read -a child-syn-read \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/filesys/base/child-syn-wrt -a child-syn-wrt \
-p tests/filesys/base/syn-write -a syn-write \
-p tests/filesys/base/syn-remove -a syn-remove \
-p tests/filesys/extended/grow-file-size -a grow-file-size \
-- -q  -f run grow-file-size
