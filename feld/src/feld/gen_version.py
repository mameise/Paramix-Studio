#!/usr/bin/env python3
"""Keep the Paramix menu display and binary version tied to VERSION."""
import re
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def main():
    version = (ROOT / 'VERSION').read_text().strip()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?', version):
        raise ValueError('Unsupported VERSION format')
    if len(version) * 6 - 1 > 126:
        raise ValueError('About version exceeds display width')
    (HERE / 'version.h').write_text(
        '/* Generated from VERSION by gen_version.py. */\n'
        '#ifndef PARAMIX_VERSION_H\n#define PARAMIX_VERSION_H\n'
        f'#define PARAMIX_VERSION "{version}"\n'
        f'static const char paramix_version_tag[] __attribute__((used)) = "PMXVER:{version}";\n'
        '#endif\n')

if __name__ == '__main__':
    main()
