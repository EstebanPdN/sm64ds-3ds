#!/usr/bin/env python3
import pathlib
import sys
data = pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[2]).write_text(
    '#pragma once\n#include <cstdint>\nalignas(4) static const uint8_t scene_shader[] = {\n'
    + ','.join(str(x) for x in data) + '\n};\n')
