#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 \
-p tests/vm/child-qsort-mm -a child-qsort-mm \
-p tests/vm/page-merge-mm -a page-merge-mm \
-p tests/vm/page-merge-par -a page-merge-par \
-p tests/vm/child-sort -a child-sort \
-p tests/vm/child-linear -a child-linear \
-p tests/vm/page-parallel -a page-parallel \
-p tests/filesys/extended/grow-seq-lg -a grow-seq-lg \
-- -q  -f run page-parallel