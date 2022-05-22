#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/extended/dir-mk-tree -a dir-mk-tree \
-p tests/filesys/extended/grow-seq-lg -a grow-seq-lg \
-- -q  -f run dir-mk-tree
