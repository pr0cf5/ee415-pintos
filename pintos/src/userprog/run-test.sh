#!/bin/sh
cd build
pintos -v -k -T 60 --qemu --filesys-size=2 \
-p tests/userprog/wait-twice -a wait-twice \
-p tests/userprog/child-simple -a child-simple \
-- -q  -f run wait-twice