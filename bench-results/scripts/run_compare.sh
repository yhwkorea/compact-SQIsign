#!/usr/bin/env bash
# Meta-driver: parallel-N restart. All 6 blocks redone with PARALLEL=4
# so Ours vs KLKL25 are measured under identical conditions.
#
# Order:
#   1. Ours    lvl1
#   2. KLKL25  lvl1
#   3. Ours    lvl3
#   4. Ours    lvl5
#   5. KLKL25  lvl3
#   6. KLKL25  lvl5

set -u

KLKL25_BUILD="${KLKL25_BUILD:-<path>/SQIsign-Fixed-Precision/build}"
OURS_BUILD="${OURS_BUILD:-<path>/compact-SQIsign/build}"
OUT_BASE="${OUT_BASE:-<path>/sqisign-fp-bench/results_compare}"
DRIVER="${DRIVER:-<path>/sqisign-fp-bench/run_until20_parallel.sh}"
export PARALLEL="${PARALLEL:-4}"

mkdir -p "$OUT_BASE"
META_LOG="$OUT_BASE/META.log"

mlog() {
  echo "[$(date '+%F %T')] [meta] $*" >> "$META_LOG"
}

run_block() {
  local impl=$1 lvl=$2 build=$3 phases=$4
  local out_dir="$OUT_BASE/${impl}/${lvl}"
  mkdir -p "$out_dir"

  mlog ">>> START  impl=${impl}  level=${lvl}  phases=${phases}  N=${PARALLEL}  out=${out_dir}  build=${build}"

  BUILD="$build" OUT="$out_dir" LEVELS="$lvl" PHASES="$phases" \
    bash "$DRIVER" > "$out_dir/driver.stdout" 2> "$out_dir/driver.stderr"
  local rc=$?

  mlog "<<< DONE   impl=${impl}  level=${lvl}  rc=${rc}"

  if [ -f "$out_dir/progress.log" ]; then
    sed "s|^|[${impl}/${lvl}] |" "$out_dir/progress.log" >> "$META_LOG"
  fi
}

mlog "###### run_compare.sh restart (PARALLEL=$PARALLEL, full redo for fair comparison)"

run_block ours    lvl1 "$OURS_BUILD"   "keypair sign verify"
run_block klkl25  lvl1 "$KLKL25_BUILD" "keypair sign verify"
run_block ours    lvl3 "$OURS_BUILD"   "keypair sign verify"
run_block ours    lvl5 "$OURS_BUILD"   "keypair sign verify"
run_block klkl25  lvl3 "$KLKL25_BUILD" "keypair sign verify"
run_block klkl25  lvl5 "$KLKL25_BUILD" "keypair sign verify"

mlog "###### run_compare.sh end"
