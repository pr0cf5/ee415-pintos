#!/bin/sh
cd build
pintos --qemu  -- -q  run $1 < /dev/null 2> tests/threads/$1.errors > tests/threads/$1.output
perl -I../.. ../../tests/threads/$1.ck tests/threads/$1 tests/threads/$1.result