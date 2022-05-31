#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/vm/page-merge-mm -a page-merge-mm \
-p tests/vm/child-qsort-mm -a child-qsort-mm \
-- -q  -f run page-merge-mm 