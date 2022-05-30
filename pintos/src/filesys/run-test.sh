#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/extended/dir-rm-tree -a dir-rm-tree \
-p tests/filesys/extended/grow-seq-lg -a grow-seq-lg \
-- -q  -f run dir-rm-tree
