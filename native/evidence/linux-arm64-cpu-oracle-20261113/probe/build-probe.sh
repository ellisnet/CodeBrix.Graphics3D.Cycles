# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /work/CodeBrix.Graphics3D.Cycles/native/work/linux-arm64-probe-20261113
set -x
/usr/bin/g++-14 -fno-strict-aliasing -std=c++17 -fPIC -fno-trapping-math -fno-math-errno -fno-signed-zeros -ffp-contract=on -freciprocal-math -fno-signaling-nans -fno-rounding-math -O3 -std=c++20 -fPIC -ffp-contract=off -fno-reciprocal-math -x c++ probe.c -o probe-kernelflags -lm
echo compile1 exit $?
gcc-14 -O2 -o probe-plain probe.c -lm; echo compile2 exit $?
set +x
A="inf 3.40282347e38 1e37 2147483648 4294967296 nan -- 0 0 -1"
./probe-kernelflags $A > out-kernelflags.txt 2>&1; echo "kernelflags exit $?"; cat out-kernelflags.txt
./probe-plain $A > out-plain.txt 2>&1; echo "plain exit $?"; cat out-plain.txt
