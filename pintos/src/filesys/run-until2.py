#!/usr/bin/env python3
from subprocess import check_output
import sys, os
from tqdm import tqdm

testname = "vm/page-parallel"

def verify(out):
    with open("tests/{}.output".format(testname), "wb") as f:
        f.write(out)
    cmdline = "perl -I../.. ../../tests/{}.ck tests/{} result.txt".format(testname, testname)
    return b'pass' in check_output(cmdline, shell=True)

if __name__ == "__main__":
    os.chdir("build")
    cmdline = "pintos -v -k --qemu --filesys-size=2 --swap-size=4 "
    #cmdline += "-p tests/vm/child-qsort-mm -a child-qsort-mm " 
    #cmdline += "-p tests/vm/page-merge-mm -a page-merge-mm " 
    #cmdline += "-- -q  -f run page-merge-mm "
    cmdline += "-p tests/vm/child-linear -a child-linear " 
    cmdline += "-p tests/vm/page-parallel -a page-parallel " 
    cmdline += "-- -q  -f run page-parallel "

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
            break

    print("[+] %d rounds of execution complete, %d times failed"%(exec_cnt, exec_cnt-pass_cnt))
    with open("error.log", "wb") as f:
        f.write(error_log)