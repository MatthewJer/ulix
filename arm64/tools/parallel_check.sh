#!/bin/bash
# usage: parallel_check.sh [JOBS]      (default: JOBS=6; also: make pcheck JOBS=8)
# The same tests as "make check", but every test is its own qemu instance with its own log in check-logs/, and up to
# JOBS of them run at the same time (they are independent: -snapshot disk, own log files, own TCP ports).
# Prints one line per test in a fixed order and exits with status 0 if all passed.
# Needs: ulix.bin ulix-console.bin ulix-shell.bin disk.img (built by "make pcheck").
cd "$(dirname "$0")/.." || exit 1
DISK=${DISK:-disk.img}
DISK2=${DISK2:-disk2.img}
D2="-drive file=$DISK2,format=raw,if=none,id=hd1 -device virtio-blk-device,drive=hd1"
DISK3=${DISK3:-disk3.img}
D3="-drive file=$DISK3,format=raw,if=none,id=hd2 -device virtio-blk-device,drive=hd2"
L=check-logs
QEMU=qemu-system-aarch64

# name|description|kind|machine|extra qemu arguments|RAM in MByte     (longest jobs first)
JOBLIST=(
  "ram3072|RAM 3072 MByte, 2 CPUs|kernel|virt|-smp 2|3072"
  "ram1024|RAM 1024 MByte, 2 CPUs|kernel|virt|-smp 2|1024"
  "shell|shell session (2 CPUs, GICv3)|shell|virt,gic-version=3|-smp 2|128"
  "console|console test (2 CPUs, GICv3)|console|virt,gic-version=3|-smp 2|128"
  "verify|write tests, then file system check of the disk copy|verify|virt||128"
  "two|two disks (hda and hdb on /tmp), 2 CPUs|kernel|virt|-smp 2 @D2@|128"
  "shell2|shell session with two disks|shell|virt,gic-version=3|-smp 2 @D2@|128"
  "three|three disks (hda, hdb on /tmp, fd1 on /mnt), 2 CPUs|kernel|virt|-smp 2 @D2@ @D3@|128"
  "shell3|shell session with three disks|shell|virt,gic-version=3|-smp 2 @D2@ @D3@|128"
  "sdisk|serial disk on a second UART (host program serves disk3.img), 2 CPUs|sdisk|virt|-smp 2|128"
  "rpi4|Raspberry Pi 4B in qemu raspi4b: kernel tests, SD card, device tree with ranges|rpi4|raspi4b||128"
  "rpi4shell|Raspberry Pi 4B in qemu raspi4b: shell session|rpi4shell|raspi4b||128"
  "rpi4console|Raspberry Pi 4B in qemu raspi4b: console test|rpi4console|raspi4b||128"
  "ram512|RAM 512 MByte, 2 CPUs|kernel|virt|-smp 2|512"
  "ram64|RAM 64 MByte, 2 CPUs|kernel|virt|-smp 2|64"
  "el1|EL1, virtio legacy transport|kernel|virt||128"
  "el2|EL2, virtio modern transport|kernel|virt,virtualization=on|-global virtio-mmio.force-legacy=false|128"
  "m-v2-1|GICv2, 1 CPU|kernel|virt,gic-version=2|-smp 1|128"
  "m-v3-1|GICv3, 1 CPU|kernel|virt,gic-version=3|-smp 1|128"
  "m-v2-2|GICv2, 2 CPUs|kernel|virt,gic-version=2|-smp 2|128"
  "m-v3-2|GICv3, 2 CPUs|kernel|virt,gic-version=3|-smp 2|128"
  "m-v2-4|GICv2, 4 CPUs|kernel|virt,gic-version=2|-smp 4|128"
  "m-v3el2-4|GICv3 at EL2, 4 CPUs|kernel|virt,gic-version=3,virtualization=on|-smp 4|128"
  "m-v3el2-2|GICv3 at EL2, 2 CPUs|kernel|virt,gic-version=3,virtualization=on|-smp 2|128"
)

# ---- one job: writes $L/NAME.log and $L/NAME.res ("ok" or "FAILED", then a one line summary) ----
if [ "$1" = "--job" ]; then
  for j in "${JOBLIST[@]}"; do [ "${j%%|*}" = "$2" ] && IFS='|' read -r name desc kind machine extra mem <<< "$j"; done
  extra=${extra//@D2@/$D2}; extra=${extra//@D3@/$D3}
  log=$L/$name.log; res=$L/$name.res; rm -f "$res"
  case $kind in
    kernel)
      sum=$(QEMU_MEM=$mem tools/run_kernel_tests.sh ulix.bin $DISK "$log" -M $machine $extra | tr '\n' ' ' | sed 's/RESULT: //')
      grep -q "^RESULT: all checks passed" "$log" && st=ok || st=FAILED ;;
    shell|console)
      case $name in shell3) port=45127;; shell2) port=45126;; *) if [ $kind = shell ]; then port=45125; else port=45123; fi;; esac
      HAVE_DISK2=$([[ $extra == *$DISK2* ]] && echo 1) HAVE_DISK3=$([[ $extra == *$DISK3* ]] && echo 1) QEMU_PORT=$port QEMU_M=$machine QEMU_MEM=$mem QEMU_EXTRA="$extra" python3 tools/${kind}_test.py ulix-$kind.bin $DISK > "$log" 2>&1
      rc=$?; sum=$(tail -1 "$log")
      [ $rc -eq 0 ] && grep -q "all passed" <<< "$sum" && st=ok || st=FAILED ;;
    sdisk)
      QEMU_PORT=45131 QEMU_M=$machine QEMU_EXTRA="$extra" python3 tools/sdisk_test.py ulix.bin $DISK $DISK3 > "$log" 2>&1
      rc=$?; sum=$(tail -1 "$log")
      [ $rc -eq 0 ] && grep -q "all passed" <<< "$sum" && st=ok || st=FAILED ;;
    rpi4)
      # the Raspberry Pi 4B image on qemu's raspi4b machine with the SD card (the disks are its partitions): the kernel tests
      rm -f "$log"
      $QEMU -M raspi4b -dtb rpi4-qemu.dtb -serial file:"$log" -monitor none -display none -kernel kernel8.img -snapshot \
        -drive file=sd.img,if=sd,format=raw 2>/dev/null &
      P=$!
      for i in $(seq 1 240); do sleep 1; grep -q '^RESULT' "$log" 2>/dev/null && break; done
      kill $P 2>/dev/null; wait $P 2>/dev/null
      sum="$(grep -c '^  ok:' "$log") checks ok, $(grep -c 'FAIL' "$log") failed $(grep '^RESULT' "$log" | sed 's/RESULT: //')"
      if grep -q "^RESULT: all checks passed" "$log" && grep -q 'GIC: version 2, distributor at 0xff841000' "$log" \
         && grep -q '^sd: card ready' "$log" && grep -q 'RAM: 960 MByte at 0x0' "$log"; then st=ok; else st=FAILED; fi ;;
    rpi4shell|rpi4console)
      case $kind in rpi4shell) img=kernel8-shell.img; script=shell_test; port=45133;; *) img=kernel8-console.img; script=console_test; port=45132;; esac
      QEMU_BOARD=rpi4 HAVE_DISK2=1 HAVE_DISK3=1 QEMU_PORT=$port python3 tools/$script.py $img sd.img > "$log" 2>&1
      rc=$?; sum=$(tail -1 "$log")
      [ $rc -eq 0 ] && grep -q "all passed" <<< "$sum" && st=ok || st=FAILED ;;
    verify)
      VD=$(mktemp -d)                                  # (the copies are written to: keep them off a slow SD card)
      cp $DISK $VD/disk-after.img; cp $DISK2 $VD/disk2-after.img; cp $DISK3 $VD/disk3-after.img
      $QEMU -M virt -cpu cortex-a72 -m 128 -display none -serial file:"$log" -monitor none -kernel ulix.bin \
        -drive file=$VD/disk-after.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 \
        -drive file=$VD/disk2-after.img,format=raw,if=none,id=hd1 -device virtio-blk-device,drive=hd1 \
        -drive file=$VD/disk3-after.img,format=raw,if=none,id=hd2 -device virtio-blk-device,drive=hd2 2>/dev/null &
      P=$!
      for i in $(seq 1 400); do sleep 1; grep -q "^RESULT" "$log" 2>/dev/null && break; done
      sleep 1; kill $P 2>/dev/null; wait $P 2>/dev/null
      python3 tools/minix_check.py $VD/disk-after.img --files > $L/verify-fs.log 2>&1
      python3 tools/minix_check.py $VD/disk2-after.img --files > $L/verify-fs2.log 2>&1
      python3 tools/minix_check.py $VD/disk3-after.img --files > $L/verify-fs3.log 2>&1
      rm -rf $VD
      sum="$(grep '^RESULT' "$log" | tr '\n' ' ')$(grep -E 'PROBLEMS|leaked' $L/verify-fs.log $L/verify-fs2.log $L/verify-fs3.log | sed 's/^[^:]*://' | tr '\n' ' ')"
      if grep -q "^RESULT: all checks passed" "$log" && ! grep -q 'PROBLEM:' $L/verify-fs.log $L/verify-fs2.log $L/verify-fs3.log && grep -q 'leaked zones.*: 0,' $L/verify-fs.log && grep -q 'leaked zones.*: 0,' $L/verify-fs2.log && grep -q 'leaked zones.*: 0,' $L/verify-fs3.log; then st=ok; else st=FAILED; fi ;;
  esac
  echo "$st $sum" > "$res"
  exit 0
fi

# ---- driver ----
JOBS=${1:-${JOBS:-6}}
mkdir -p $L; rm -f $L/*.res
start=$(date +%s)
printf '%s\n' "${JOBLIST[@]}" | cut -d'|' -f1 | xargs -P "$JOBS" -I{} bash "$0" --job {}
fails=0
for j in "${JOBLIST[@]}"; do
  IFS='|' read -r name desc rest <<< "$j"
  r=$(cat $L/$name.res 2>/dev/null || echo "FAILED (no result)")
  printf '  %-52s %s\n' "$desc" "$r"
  case $r in ok*) ;; *) fails=$((fails + 1)) ;; esac
done
echo "  $(( $(date +%s) - start )) s with $JOBS parallel jobs, logs in $L/"
[ $fails -eq 0 ] && echo "== ALL TESTS PASSED" || echo "== $fails TEST(S) FAILED"
exit $((fails > 0))
