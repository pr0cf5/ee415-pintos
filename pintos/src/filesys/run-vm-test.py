#!/usr/bin/env python3
from subprocess import check_output
import sys, os
from tqdm import tqdm

children_list = {
    "page-parallel" : "child-linear",
    "page-merge-mm" : "child-qsort-mm",
    "page-merge-stk" : "child-qsort",
    "page-merge-par" : "child-sort",
    "page-merge-seq" : "child-sort",
}

def verify(testcase, out):
    with open("tests/vm/{}.output".format(testcase), "wb") as f:
        f.write(out)
    cmdline = "perl -I../.. ../../tests/vm/{}.ck tests/vm/{} result.txt".format(testcase, testcase)
    return b'pass' in check_output(cmdline, shell=True)

def test_vm(testcase, runs):
    cmdline = "pintos -v -k --qemu --filesys-size=2 --swap-size=4 "
    cmdline += "-p tests/vm/{} -a {} ".format(children_list[testcase], children_list[testcase]) 
    cmdline += "-p tests/vm/{} -a {} ".format(testcase, testcase) 
    cmdline += "-- -q  -f run {} ".format(testcase)

    error_log = b""
    pass_cnt = 0
    total_cnt = 0

    for i in tqdm(range(runs)):
        total_cnt += 1
        out = check_output(cmdline, shell=True)
        if verify(testcase, out):
            pass_cnt += 1
        else:
            print("[!] Error detected")
            error_log += b"="*30 + b"\n"
            error_log += out
            error_log += b"="*30 + b"\n"
            break

    print("[+] %d rounds of execution complete, %d times failed"%(total_cnt, total_cnt-pass_cnt))
    with open("error-{}.log".format(testcase), "wb") as f:
        f.write(error_log)

if __name__ == "__main__":
    os.chdir("build")
    for t in ["page-merge-mm"]:
        print("Testing: {}".format(t))
        test_vm(t, 1000)

    
