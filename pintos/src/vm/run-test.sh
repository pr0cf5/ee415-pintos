#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=2 --swap-size=4 \
-p tests/userprog/sc-bad-sp -a sc-bad-sp \
-p tests/userprog/args-none -a args-none \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/vm/page-merge-mm -a page-merge-mm \
-p tests/vm/child-sort -a child-sort \
-p tests/vm/page-merge-stk -a page-merge-stk \
-p tests/vm/child-qsort-mm -a child-qsort-mm \
-p tests/vm/pt-grow-stack -a pt-grow-stack \
-p tests/vm/pt-big-stk-obj -a pt-big-stk-obj \
-p tests/vm/pt-grow-stk-sc -a pt-grow-stk-sc \
-p tests/vm/mmap-over-stk -a mmap-over-stk \
-p ../../tests/vm/sample.txt -a sample.txt \
-p tests/userprog/child-sig -a child-sig \
-p tests/userprog/sig-simple -a sig-simple \
-- -q  -f run sig-simple
