#!/bin/sh
# PC test runner (gcc): ./host/wolfhost
set -e
cd "$(dirname "$0")/.."
gcc -O1 -g -w -fno-pie -DPARAMIX -Iw4 -Ipmx $XCF w4/*.c pmx/*.c host/host.c -lm -no-pie -o host/wolfhost
