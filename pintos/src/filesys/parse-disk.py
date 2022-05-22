#!/usr/bin/python3
import struct
import sys

def u32(x):
    return struct.unpack("<I", x)[0]

def u8(x):
    return struct.unpack("<B", x)[0]

def parse_dir(sector):
    d = disk[(sector+1)*SECTOR_SZ:(sector+2)*SECTOR_SZ]
    start = u32(d[0:4])
    magic = u32(d[0x10:0x14])
    size = u32(d[8:12])
    size_type = u8(d[12:13])
    func_type = u8(d[13:14])
    inner = disk[(start+1)*SECTOR_SZ:(start+2)*SECTOR_SZ]
    sectors = []
    for ofs in range(0, SECTOR_SZ, 20):
        if ofs + 20 >= SECTOR_SZ:
            break
        in_use = u8(inner[ofs+19:ofs+20])
        x = u32(inner[ofs:ofs+4])
        if in_use:
            sectors.append(x)
    print("directory(@{}): start = {}, magic = {}, size = {}, size_type = {}, func_type = {}".format(sector, start, hex(magic), size, size_type, func_type))
    print("sectors: {}".format(str(sectors)))

def parse_file(sector):
    d = disk[(sector+1)*SECTOR_SZ:(sector+2)*SECTOR_SZ]
    start = u32(d[0:4])
    magic = u32(d[0x10:0x14])
    size = u32(d[8:12])
    size_type = u8(d[12:13])
    func_type = u8(d[13:14])
    print("file(@{}): start = {}, magic = {}, size = {}, size_type = {} func_type = {}".format(sector, start, hex(magic), size, size_type, func_type))

SECTOR_SZ = 0x200

if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        disk = f.read()

    root_sector = disk[2*SECTOR_SZ:2*SECTOR_SZ+SECTOR_SZ]
    # /0
    parse_dir(1)
    # /0/0
    parse_file(392)

