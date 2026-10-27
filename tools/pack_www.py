#!/usr/bin/env python3
"""
Prepares the web interface (www/) for the "www" SPIFFS partition: the text files (html, js, css, svg,
json) are stored gzip-compressed as <name>.gz (the dash sends them with Content-Encoding: gzip, the
browser unpacks them), everything else (fonts, pictures - already compressed) is copied as it is.
It makes the pages load 2-3 times faster over the Wi-Fi of the board and leaves room in the partition.

usage: pack_www.py <www dir> <output dir>
Called by CMakeLists.txt when the project is configured; the output directory is rebuilt every time.
"""
import gzip
import shutil
import sys
from pathlib import Path

TEXT = {".html", ".js", ".css", ".svg", ".json"}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    before = after = 0
    for p in sorted(src.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(src)
        out = dst / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        data = p.read_bytes()
        before += len(data)
        if p.suffix.lower() in TEXT:
            out = out.with_name(out.name + ".gz")
            data = gzip.compress(data, compresslevel=9, mtime=0)  # mtime 0: the same input gives the same file
        out.write_bytes(data)
        after += len(data)
    print(f"pack_www: {before // 1024} KB -> {after // 1024} KB")


if __name__ == "__main__":
    main()
