#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/vm/page-merge-mm -a page-merge-mm \
-p tests/vm/child-qsort -a child-qsort \
-p tests/vm/page-merge-stk -a page-merge-stk \
-p tests/vm/child-qsort-mm -a child-qsort-mm \
-p tests/userprog/rox-multichild -a rox-multichild \
-p tests/userprog/child-rox -a child-rox \
-p tests/vm/mmap-write -a mmap-write \
-- -q  -f run page-merge-mm
