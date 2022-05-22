#!/bin/sh
cd build
rm -f tmp.dsk
pintos-mkdisk tmp.dsk --filesys-size=2
pintos -v -k -T 60 --qemu --swap-size=4  --disk=tmp.dsk \
 -p tests/filesys/extended/dir-mk-tree -a dir-mk-tree \
-- -q  -f run dir-mk-tree > ../output.log
pintos -v -k -T 60 --qemu --swap-size=4  --disk=tmp.dsk \
 -p tests/filesys/extended/tar -a tar \
-- -q  -f run 'tar fs.tar /'