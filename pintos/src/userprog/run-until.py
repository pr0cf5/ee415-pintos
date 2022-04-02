#!/usr/bin/env python3
from subprocess import check_output
import sys, os

def verify(out):
    return b"syn-read: exit(0)" in out

if __name__ == "__main__":
    os.chdir("build")
    cmdline = "pintos -v -k -T 60 --qemu --filesys-size=2 "
    cmdline += "-p tests/userprog/exec-missing -a exec-missing " 
    cmdline += "-p tests/filesys/base/syn-write -a syn-write " 
    cmdline += "-p tests/filesys/base/syn-read -a syn-read "
    cmdline += "-p tests/filesys/base/child-syn-wrt -a child-syn-wrt "
    cmdline += "-p tests/filesys/base/child-syn-read -a child-syn-read "
    cmdline += "-p ../../tests/userprog/sample.txt -a sample.txt "
    cmdline += "-- -q  -f run syn-read "
    
    error_log = b""
    pass_cnt = 0
    exec_cnt = 1000

    for i in range(exec_cnt):
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