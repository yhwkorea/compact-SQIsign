#!/usr/bin/env bash
# Parallel drop-in for run_until20.sh.

set -u

REPO="${REPO:-<path>/SQIsign-Fixed-Precision}"
BUILD="${BUILD:-$REPO/build}"
OUT="${OUT:-<path>/sqisign-fp-bench/results_compare}"

TIMEOUT_LVL1="${TIMEOUT_LVL1:-350}"   # ~1.5×(parallel) × 1.2×(safety) × 184s max-observed legit DUR
TIMEOUT_LVL3="${TIMEOUT_LVL3:-1300}"
TIMEOUT_LVL5="${TIMEOUT_LVL5:-2600}"

VALID_TARGET="${VALID_TARGET:-20}"
HARD_ATTEMPT_CAP="${HARD_ATTEMPT_CAP:-300}"
PARALLEL="${PARALLEL:-4}"

LEVELS="${LEVELS:-lvl1 lvl3 lvl5}"
PHASES="${PHASES:-keypair sign verify}"

mkdir -p "$OUT"
cd "$BUILD"
ulimit -s unlimited

PROGRESS="$OUT/progress.log"

log() {
  echo "[$(date '+%F %T')] $*" | tee -a "$PROGRESS"
}

extract_field() {
  local f=$1 name=$2 field=$3
  awk -v n="$name" -v f="$field" '
    $0 ~ "^  "n" " && $0 ~ f {
      for (i = 1; i <= NF; i++) if ($i == f) { print $(i+1); exit }
    }
  ' "$f"
}

run_phase_parallel() {
  local level=$1 timeout_s=$2 phase=$3
  local lvl_out="$OUT/$level"
  mkdir -p "$lvl_out"

  local summary="$lvl_out/SUMMARY_${phase}.tsv"
  printf 'iter\tdur_s\texit\tretries\tphase_mcyc\tphase_wall_s\n' > "$summary"

  local state_dir="$lvl_out/.state_${phase}"
  rm -rf "$state_dir"; mkdir -p "$state_dir"
  echo 0 > "$state_dir/attempt"
  echo 0 > "$state_dir/valid"
  local lockfile="$state_dir/.lock"
  : > "$lockfile"

  log "=== $level / $phase start (parallel N=$PARALLEL): target=$VALID_TARGET, hard_cap=$HARD_ATTEMPT_CAP, per-iter timeout=${timeout_s}s"

  # Export so subshells can see them
  export OUT VALID_TARGET HARD_ATTEMPT_CAP PROGRESS state_dir lockfile lvl_out summary
  export level phase timeout_s

  local pids=()
  for ((w=0; w<PARALLEL; w++)); do
    (
      core=$w
      while true; do
        # Atomically claim next attempt (or STOP)
        claim=$(
          (
            flock 9
            a=$(cat "$state_dir/attempt")
            v=$(cat "$state_dir/valid")
            if [ "$v" -ge "$VALID_TARGET" ] || [ "$a" -ge "$HARD_ATTEMPT_CAP" ]; then
              echo "STOP"
            else
              attempt=$((a + 1))
              echo "$attempt" > "$state_dir/attempt"
              echo "$attempt"
            fi
          ) 9>"$lockfile"
        )
        [ "$claim" = "STOP" ] && break

        attempt=$claim
        lg="$lvl_out/${phase}_iter${attempt}.log"
        er="$lvl_out/${phase}_iter${attempt}.err"
        t0=$(date +%s)
        taskset -c "$core" timeout "$timeout_s" \
          ./apps/benchmark_${level} --iterations=1 --only=${phase} > "$lg" 2> "$er"
        rc=$?
        t1=$(date +%s)
        dur=$((t1 - t0))

        retries=$(awk '/\[SIGN_RETRIES\]/ {print $2}' "$er" 2>/dev/null | tail -1)
        [ -z "$retries" ] && retries="?"
        m_val=$(extract_field "$lg" "$phase" median)
        w_val=$(extract_field "$lg" "$phase" wall_med)

        # Atomic: append to SUMMARY + increment valid if exit 0 + write progress line
        (
          flock 9
          printf '%d\t%d\t%d\t%s\t%s\t%s\n' "$attempt" "$dur" "$rc" "$retries" "${m_val:-?}" "${w_val:-?}" >> "$summary"
          if [ "$rc" -eq 0 ]; then
            v=$(cat "$state_dir/valid")
            v=$((v + 1))
            echo "$v" > "$state_dir/valid"
            echo "[$(date '+%F %T')]   $level / $phase iter=$attempt core=$core dur=${dur}s exit=0 retries=$retries wall=${w_val:-?}s  (valid=$v/$VALID_TARGET)" >> "$PROGRESS"
          else
            echo "[$(date '+%F %T')]   $level / $phase iter=$attempt core=$core dur=${dur}s exit=$rc — not counted" >> "$PROGRESS"
          fi
        ) 9>"$lockfile"
      done
    ) &
    pids+=($!)
  done

  wait "${pids[@]}"

  local fv fa
  fv=$(cat "$state_dir/valid")
  fa=$(cat "$state_dir/attempt")
  log "=== $level / $phase done: $fv valid / $fa attempts (parallel N=$PARALLEL)"
}

run_level() {
  local level=$1 timeout_s=$2
  for phase in $PHASES; do
    run_phase_parallel "$level" "$timeout_s" "$phase"
  done
}

log "###### run_until20_parallel.sh start"
log "      REPO=$REPO BUILD=$BUILD OUT=$OUT"
log "      LEVELS=$LEVELS  PHASES=$PHASES  PARALLEL=$PARALLEL"
log "      VALID_TARGET=$VALID_TARGET  HARD_ATTEMPT_CAP=$HARD_ATTEMPT_CAP"
log "      timeouts: lvl1=${TIMEOUT_LVL1}s lvl3=${TIMEOUT_LVL3}s lvl5=${TIMEOUT_LVL5}s"

for lvl in $LEVELS; do
  case "$lvl" in
    lvl1) run_level lvl1 "$TIMEOUT_LVL1" ;;
    lvl3) run_level lvl3 "$TIMEOUT_LVL3" ;;
    lvl5) run_level lvl5 "$TIMEOUT_LVL5" ;;
    *) log "!!! unknown level: $lvl"; exit 1 ;;
  esac
done

log "###### run_until20_parallel.sh end"
