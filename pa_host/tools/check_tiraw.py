#!/usr/bin/env python3
"""检查 TiRayRaw 样例文件头。

这个脚本只用于开发期验证旧 Windows 上位机保存的 .tiraw 文件格式。
正式上位机程序使用 C++ 的 TiRawImage 类读取图像。
"""

import os
import struct
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_tiraw.py <image.tiraw>", file=sys.stderr)
        return 2

    path = sys.argv[1]
    with open(path, "rb") as f:
        header = f.read(16)

    if len(header) != 16:
        print("invalid: header too short")
        return 1

    magic, version, bytes_per_pixel, height, width = struct.unpack("<8sHHHH", header)
    size = os.path.getsize(path)
    expected = 16 + width * height * bytes_per_pixel

    print(f"path={path}")
    print(f"magic={magic!r}")
    print(f"version={version}")
    print(f"bytes_per_pixel={bytes_per_pixel}")
    print(f"width={width}")
    print(f"height={height}")
    print(f"file_size={size}")
    print(f"expected_size={expected}")
    print(f"size_match={size == expected}")

    return 0 if magic == b"TiRayRaw" and size == expected else 1


if __name__ == "__main__":
    raise SystemExit(main())

