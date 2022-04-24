#!/bin/sh
cd build
pintos -v -k --qemu --gdb --filesys-size=2 --swap-size=4 \
-p tests/userprog/sc-bad-sp -a sc-bad-sp \
-p tests/userprog/args-none -a args-none \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/vm/page-merge-mm -a page-merge-mm \
-p tests/vm/child-sort -a child-sort \
-p tests/vm/page-merge-stk -a page-merge-stk \
-p tests/vm/child-qsort -a child-qsort \
-- -q  -f run page-merge-stk
