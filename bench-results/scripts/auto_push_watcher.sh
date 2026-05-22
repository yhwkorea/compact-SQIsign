#!/bin/bash
# Watcher: tails META.log; whenever a new "<<< DONE" line appears (meta-driver
# finished a block), rsync the bench results into compact-SQIsign/bench-results
# and push to origin/main.
#
# Stops on its own when META.log shows "###### run_compare.sh end".

set -u

META=/root/sqisign-fp-bench/results_compare/META.log
RESULTS=/root/sqisign-fp-bench/results_compare
REPO=/root/compact-SQIsign
PUSH_LOG=/root/sqisign-fp-bench/auto_push.log

: > "$PUSH_LOG"

log() { echo "[$(date '+%F %T')] $*" >> "$PUSH_LOG"; }

last_seen=0
log "###### auto_push_watcher start (META=$META)"

while true; do
  if [ ! -f "$META" ]; then sleep 10; continue; fi
  cur=$(grep -c '<<< DONE' "$META" 2>/dev/null || echo 0)

  if [ "$cur" -gt "$last_seen" ]; then
    block_line=$(grep '<<< DONE' "$META" | tail -1)
    log "new block done: $block_line"

    cd "$REPO" || { log "FATAL: cannot cd $REPO"; exit 1; }

    rsync -a --exclude='*_iter*.log' --exclude='*_iter*.err' --exclude='driver.std*' \
      "$RESULTS/" bench-results/results_compare/ 2>&1 | tail -3 >> "$PUSH_LOG"
    # Filter SUMMARY tsv: drop non-zero-exit rows (timeout/abort) AND trim
    # to first 20 valid samples (parallel workers can overshoot the target).
    find bench-results/results_compare -name 'SUMMARY_*.tsv' -print0 | \
      while IFS= read -r -d '' f; do
        head -1 "$f" > "$f.new"
        awk -F'\t' 'NR>1 && $3==0 { c++; if (c<=20) print }' "$f" >> "$f.new"
        mv "$f.new" "$f"
      done

    # Refresh STATS.md (mean/median/min/max per impl/level/phase)
    bash /root/sqisign-fp-bench/compute_stats.sh "$RESULTS" "$REPO/bench-results/results_compare" 2>>"$PUSH_LOG"

    git add bench-results/ 2>>"$PUSH_LOG"
    if git -c user.email=anonymous@example.com -c user.name=Anonymous \
         commit -m "bench-results: $(echo "$block_line" | sed 's/.*<<< DONE //')" \
         2>>"$PUSH_LOG"; then
      timeout 180 git push origin main 2>>"$PUSH_LOG" \
        && log "push OK" \
        || log "push FAIL"
    else
      log "nothing to commit (no new diff)"
    fi

    last_seen=$cur
  fi

  # Termination check
  if grep -q '###### run_compare.sh end' "$META" 2>/dev/null; then
    log "###### detected run_compare end — final push then exit"
    # Final push for the very last block
    cd "$REPO"
    rsync -a --exclude='*_iter*.log' --exclude='*_iter*.err' --exclude='driver.std*' \
      "$RESULTS/" bench-results/results_compare/ 2>&1 | tail -3 >> "$PUSH_LOG"
    # Filter SUMMARY tsv: drop non-zero-exit rows (timeout/abort) AND trim
    # to first 20 valid samples (parallel workers can overshoot the target).
    find bench-results/results_compare -name 'SUMMARY_*.tsv' -print0 | \
      while IFS= read -r -d '' f; do
        head -1 "$f" > "$f.new"
        awk -F'\t' 'NR>1 && $3==0 { c++; if (c<=20) print }' "$f" >> "$f.new"
        mv "$f.new" "$f"
      done
    bash /root/sqisign-fp-bench/compute_stats.sh "$RESULTS" "$REPO/bench-results/results_compare" 2>>"$PUSH_LOG"
    git add bench-results/
    git -c user.email=anonymous@example.com -c user.name=Anonymous \
        commit -m "bench-results: final dump (all blocks done)" 2>>"$PUSH_LOG" || true
    timeout 180 git push origin main 2>>"$PUSH_LOG" || log "final push FAIL"
    log "###### auto_push_watcher exit"
    exit 0
  fi

  sleep 30
done
