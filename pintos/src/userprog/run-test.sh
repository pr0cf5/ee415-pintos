#!/bin/sh
cd build
pintos -v -k -T 60 --qemu --filesys-size=2 \
-p tests/userprog/wait-twice -a wait-twice \
-p tests/userprog/child-simple -a child-simple \
-p tests/userprog/args-multiple -a args-multiple \
-p tests/userprog/args-dbl-space -a args-dbl-space \
-p tests/userprog/close-stdin -a close-stdin \
-p tests/userprog/read-bad-ptr -a read-bad-ptr \
-p ../../tests/userprog/sample.txt -a sample.txt \
-- -q  -f run read-bad-ptr

