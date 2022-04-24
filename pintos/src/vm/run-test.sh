#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=2 --swap-size=4 \
-p tests/userprog/sc-bad-sp -a sc-bad-sp \
-p tests/userprog/args-none -a args-none \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/vm/page-linear -a page-linear \
-p ../../tests/vm/sample.txt -a sample.txt \
-- -q  -f run page-linear
