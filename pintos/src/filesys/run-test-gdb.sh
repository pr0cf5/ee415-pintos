#!/bin/sh
cd build
pintos -v -k --qemu --gdb --filesys-size=20 --swap-size=4 \
-p tests/filesys/extended/dir-rm-cwd -a dir-rm-cwd \
-- -q  -f run dir-rm-cwd
