#!/bin/bash
# usage: run_kernel_tests.sh KERNEL.elf DISK.img LOG [extra qemu arguments]
# Boots the kernel with the test suites, waits for the final "RESULT" line (at most 180 s), prints a summary.
# Exit status 0 if all checks passed.
K=$1; D=$2; L=$3; shift 3
rm -f "$L"
# QEMU_FAST=1: hardware acceleration (hvf) on an ARM Mac, about ten times faster, for the development loop only
# (the CPU is the host's, not a Cortex-A72; "make pcheck" and the final run use the emulated A72; only -M virt)
CPU="-cpu cortex-a72"; [ "$QEMU_FAST" = 1 ] && CPU="-accel hvf -cpu host"
qemu-system-aarch64 -M virt $CPU -m ${QEMU_MEM:-128} -display none -serial file:"$L" -monitor none -kernel "$K" -snapshot \
  -drive file="$D",format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 "$@" 2>/dev/null &
P=$!
for i in $(seq 1 450); do sleep 1; grep -q "^RESULT" "$L" 2>/dev/null && break; done
kill $P 2>/dev/null; wait $P 2>/dev/null
echo "  $(grep -c '^  ok:' "$L") checks ok, $(grep -c 'FAIL' "$L") failed"
grep -E "FAIL|^RESULT" "$L"
grep -q "^RESULT: all checks passed" "$L"
