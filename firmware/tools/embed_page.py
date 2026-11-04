#!/usr/bin/env python3
"""Embed the settings page (config/KMSWITCH.HTM) gzip-compressed as a C array: embed_page.py in.htm out.c"""
import gzip, sys
data = gzip.compress(open(sys.argv[1], 'rb').read(), compresslevel=9, mtime=0)
rows = [', '.join('0x%02x' % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
open(sys.argv[2], 'w').write(
    '// generated from config/KMSWITCH.HTM by tools/embed_page.py\n#include <stddef.h>\n#include <stdint.h>\n'
    'const uint8_t page_gz[] = {\n    ' + ',\n    '.join(rows) + '\n};\nconst size_t page_gz_len = %d;\n' % len(data))
