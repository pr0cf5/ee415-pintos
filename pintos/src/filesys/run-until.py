#!/usr/bin/env python3
from subprocess import check_output
import sys, os
from tqdm import tqdm

def verify(out):
    with open("result.txt", "wb") as f:
        f.write(out)
    cmdline = "perl -I../.. ../../tests/filesys/base/syn-write.ck tests/filesys/base/syn-write result.txt"
    return b'pass' in check_output(cmdline, shell=True)

'''
pintos -v -k -T 300 --qemu  --filesys-size=2 -p tests/filesys/base/syn-read -a syn-read -p tests/filesys/base/child-syn-read -a child-syn-read --swap-size=4 -- -q  t
perl -I../.. ../../tests/filesys/base/syn-read.ck tests/filesys/base/syn-read tests/filesys/base/syn-read.result
pass tests/filesys/base/syn-read
pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/filesys/base/syn-remove -a syn-remove --swap-size=4 -- -q  -f run syn-remove < /dev/null 2> tests/filesys/base/t
perl -I../.. ../../tests/filesys/base/syn-remove.ck tests/filesys/base/syn-remove tests/filesys/base/syn-remove.result
pass tests/filesys/base/syn-remove
pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/filesys/base/syn-write -a syn-write -p tests/filesys/base/child-syn-wrt -a child-syn-wrt --swap-size=4 -- -q  -t
perl -I../.. ../../tests/filesys/base/syn-write.ck tests/filesys/base/syn-write tests/filesys/base/syn-write.result
pass tests/filesys/base/syn-write
'''

if __name__ == "__main__":
    os.chdir("build")
    cmdline = "pintos -v -k --qemu --filesys-size=20 --swap-size=4 "
    cmdline += "-p tests/filesys/base/child-syn-wrt -a child-syn-wrt " 
    cmdline += "-p tests/filesys/base/syn-write -a syn-write " 
    cmdline += "-- -q  -f run syn-write"
    
    error_log = b""
    pass_cnt = 0
    exec_cnt = 1000

    for i in tqdm(range(exec_cnt)):
        out = check_output(cmdline, shell=True)
        if verify(out):
            pass_cnt += 1
        else:
            print("[-] problem detected")
            error_log += b"="*30 + b"\n"
            error_log += out
            error_log += b"="*30 + b"\n"

    print("[+] %d rounds of execution complete, %d times failed"%(exec_cnt, exec_cnt-pass_cnt))
    with open("error.log", "wb") as f:
        f.write(error_log)