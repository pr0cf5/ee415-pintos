#!/bin/sh
cd build
pintos -v -k --qemu --filesys-size=2 --swap-size=4 \
-p tests/userprog/sc-bad-sp -a sc-bad-sp \
-p tests/userprog/args-none -a args-none \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/filesys/base/child-syn-wrt -a child-syn-wrt \
-p tests/filesys/base/child-syn-read -a child-syn-read \
-p tests/userprog/child-simple -a child-simple \
-p ../../tests/userprog/sample.txt -a sample.txt \
-- -q  -f run args-none
