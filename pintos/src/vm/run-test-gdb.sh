#!/bin/sh
cd build
pintos -v -k --qemu --gdb --filesys-size=20 --swap-size=4 \
-p tests/vm/page-merge-mm -a page-merge-mm \
-p tests/vm/child-qsort -a child-qsort \
-p tests/vm/page-merge-stk -a page-merge-stk \
-p tests/vm/child-qsort-mm -a child-qsort-mm \
-p tests/userprog/
-p ../../tests/vm/sample.txt -a sample.txt \
-- -q  -f run page-merge-mm
