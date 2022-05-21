#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/extended/dir-rm-tree -a dir-rm-tree \
-- -q  -f run dir-rm-tree
