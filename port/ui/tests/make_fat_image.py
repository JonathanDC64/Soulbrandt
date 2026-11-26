#!/usr/bin/env python3
"""An empty FAT16 drive image, for setup_folder_check's FAT case (issue #673): a drive formatted as USB sticks and
exFAT drives are, which holds no symbolic links. Written with Python's standard library only, so CI needs no mkfs:

    python3 port/ui/tests/make_fat_image.py <image> [megabytes, 40 to 500]

then, on Linux: sudo mount -o loop,uid=$(id -u),gid=$(id -g) <image> <folder>.
"""
import struct
import sys


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    megabytes = int(sys.argv[2]) if len(sys.argv) == 3 else 64
    if not 40 <= megabytes <= 500:
        print("make_fat_image: 40 to 500 MB (a FAT16 drive)", file=sys.stderr)
        return 2
    sectors = megabytes * 2048                       # of 512 bytes
    per_cluster = 16                                 # 8 KB clusters: 5,120 to 64,000 (FAT16: 4,085 to 65,524)
    reserved, fats, root_entries = 1, 2, 512
    clusters = sectors // per_cluster
    fat_sectors = (clusters + 2) * 2 // 512 + 1      # two bytes a cluster
    boot = bytearray(512)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, per_cluster, reserved, fats, root_entries,
                     sectors if sectors <= 0xFFFF else 0, 0xF8, fat_sectors, 32, 64, 0,
                     0 if sectors <= 0xFFFF else sectors)
    struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, 0x51673F17, b"SETUP CHECK", b"FAT16   ")
    boot[510:512] = b"\x55\xAA"
    fat = bytearray(fat_sectors * 512)
    fat[0:4] = b"\xF8\xFF\xFF\xFF"                  # the media byte, and the end mark
    with open(sys.argv[1], "wb") as f:
        f.write(boot)
        f.write(b"\0" * 512 * (reserved - 1))
        for _ in range(fats):
            f.write(fat)
        f.truncate(sectors * 512)                    # the root folder and the clusters: empty
    print(f"{sys.argv[1]}: FAT16, {megabytes} MB, {clusters} clusters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
