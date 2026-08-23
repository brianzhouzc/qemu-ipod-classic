#!/bin/bash
# Assault #13 phase A driver: run the deadlock probe across a range of -icount
# shift values (and the no-icount baseline) back to back, then tabulate the
# key counters. Real S5L8702 core is ARM926 @ ~216 MHz (~200 MIPS), so
# shift=2 (250 MIPS) / shift=3 (125 MIPS) bracket real timing; shift=1 (500)
# and shift=4 (62.5) probe the edges of the regime-A/B race.
#
# Success signature per run = copy32 > 0 (first native PCM sample rendered).
set +e
cd "$(dirname "$0")/../.." || exit 1
RUNNER=tools/session-dbg/run_icount.sh
SHIFTS=${*:-off 1 2 3 4}
SUM=$HOME/6g/sweep_summary.txt; : > "$SUM"
for s in $SHIFTS; do
  tag="ic$s"; [ "$s" = "off" ] && tag="base"
  echo "############## icount=$s (tag=$tag) ##############"
  timeout 200 bash "$RUNNER" "$s" "$tag" 2>&1 | tail -30
  echo "----- extract $tag -----"
  L=$HOME/6g/meas_$tag.log
  copy=$(grep -aoE 'copy32 [0-9]+' "$L" 2>/dev/null | tail -1)
  h12=$(grep -aoE ' h12 [0-9]+' "$L" 2>/dev/null | tail -1)
  ex6=$(grep -aoE ' ex6 [0-9]+' "$L" 2>/dev/null | tail -1)
  disp=$(grep -aoE ' disp [0-9]+' "$L" 2>/dev/null | tail -1)
  mbox=$(grep -aoE 'mbox52sel [0-9]+' "$L" 2>/dev/null | tail -1)
  eng=$(grep -aoE ' engine [0-9]+' "$L" 2>/dev/null | tail -1)
  printf '%-6s | %-12s | %-9s | %-9s | %-9s | %-9s | %s\n' \
    "$tag" "${copy:-copy32 ?}" "${h12:-h12 ?}" "${ex6:-ex6 ?}" \
    "${disp:-disp ?}" "${mbox:-mbox52 ?}" "${eng:-engine ?}" | tee -a "$SUM"
done
echo
echo "================ SWEEP SUMMARY ================"
printf '%-6s | %-12s | %-9s | %-9s | %-9s | %-9s | %s\n' tag copy32 h12 ex6 disp mbox52 engine
cat "$SUM"
echo "=> success = any row with copy32 > 0"
