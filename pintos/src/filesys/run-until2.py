#!/usr/bin/env python3
from subprocess import check_output
import sys, os
from tqdm import tqdm

def verify(out):
    return b"success, buf_idx=1,048,576" in out

if __name__ == "__main__":
    os.chdir("build")
    cmdline = "pintos -v -k --qemu --filesys-size=2 --swap-size=4 "
    cmdline += "-p tests/vm/child-qsort-mm -a child-qsort-mm " 
    cmdline += "-p tests/vm/page-merge-mm -a page-merge-mm " 
    cmdline += "-- -q  -f run page-merge-mm "
    
    error_log = b""
    pass_cnt = 0
    exec_cnt = 1000

    for i in tqdm(range(exec_cnt)):
        out = check_output(cmdline, shell=True)
        if verify(out):
            pass_cnt += 1
        else:
            print("problem detected")
            error_log += b"="*30 + b"\n"
            error_log += out
            error_log += b"="*30 + b"\n"

    print("[+] %d rounds of execution complete, %d times failed"%(exec_cnt, exec_cnt-pass_cnt))
    with open("error.log", "wb") as f:
        f.write(error_log)