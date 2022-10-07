#!/bin/sh

CLANG_FORMAT="clang-format13 -style=file -i"

find \
    COMMON_AI \
    Include \
    Layers \
    utils \
    xr_3da \
    xrCDB \
    xrCompress \
    xrCore \
    xrGame \
    xrParticles \
    xrSound \
    '(' \
    -name '*.c' -or \
    -name '*.cpp' -or \
    -name '*.cxx' -or \
    -name '*.h' -or \
    -name '*.hpp' \
    ')' \
    -exec $CLANG_FORMAT '{}' '+'
