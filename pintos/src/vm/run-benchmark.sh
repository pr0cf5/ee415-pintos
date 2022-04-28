#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=20 --swap-size=4 --mem=128 \
-p tests/vm/page-linear -a page-linear \
-- -q  -f run page-linear