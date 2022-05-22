#!/bin/sh
TEST=dir-mk-tree
cd build
rm -f tmp.dsk
pintos-mkdisk tmp.dsk --filesys-size=2
pintos -v -k -T 60 --qemu --swap-size=4  --disk=tmp.dsk \
 -p tests/filesys/extended/$TEST -a $TEST \
-- -q  -f run $TEST > tests/filesys/extended/$TEST.output
perl -I../.. ../../tests/filesys/extended/$TEST.ck tests/filesys/extended/$TEST tests/filesys/extended/$TEST.result
pintos -v -k -T 60 --qemu --swap-size=4  --disk=tmp.dsk \
 -p tests/filesys/extended/tar -a tar \
 -g fs.tar \
-- -q  run 'tar fs.tar /' > tests/filesys/extended/$TEST-persistence.output
 cp fs.tar tests/filesys/extended/$TEST.tar
perl -I../.. ../../tests/filesys/extended/$TEST-persistence.ck tests/filesys/extended/$TEST-persistence tests/filesys/extended/$TEST-persistence.result