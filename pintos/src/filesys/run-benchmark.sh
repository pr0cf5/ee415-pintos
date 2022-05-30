#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/filesys/base/lg-tree -a lg-tree \
-- -q  -f run lg-tree