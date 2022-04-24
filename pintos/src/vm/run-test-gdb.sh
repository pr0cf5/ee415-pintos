#!/bin/sh
cd build
pintos -v -k --gdb --qemu --filesys-size=2 --swap-size=4 \
-p tests/userprog/sc-bad-sp -a sc-bad-sp \
-p tests/userprog/args-none -a args-none \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/vm/mmap-unmap -a mmap-unmap \
-p ../../tests/vm/sample.txt -a sample.txt \
-- -q  -f run mmap-unmap
