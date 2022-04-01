#!/bin/sh
cd build
pintos -v -k -T 60 --qemu --filesys-size=2 \
-p tests/userprog/exec-missing -a exec-missing \
-p tests/filesys/base/syn-write -a syn-write \
-p tests/filesys/base/syn-read -a syn-read \
-p tests/filesys/base/child-syn-wrt -a child-syn-wrt \
-p tests/filesys/base/child-syn-read -a child-syn-read \
-p ../../tests/userprog/sample.txt -a sample.txt \
-- -q  -f run syn-read

