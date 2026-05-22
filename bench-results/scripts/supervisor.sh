#!/usr/bin/env bash
# Supervisor: keeps meta-driver alive. If run_compare.sh dies, restart it.
# Also: periodic push every PUSH_INTERVAL seconds so user sees mid-block progress.

set -u

META=/root/sqisign-fp-bench/run_compare.sh
WATCHER=/root/sqisign-fp-bench/auto_push_watcher.sh
PUSH_INTERVAL="${PUSH_INTERVAL:-1800}"   # 30 min
SUP_LOG=/root/sqisign-fp-bench/supervisor.log

log() { echo "[$(date '+%F %T')] $*" >> "$SUP_LOG"; }

: > "$SUP_LOG"
log "###### supervisor start (PUSH_INTERVAL=${PUSH_INTERVAL}s)"

cd /root/sqisign-fp-bench

last_push=0

while true; do
  # ensure meta-driver alive
  if ! pgrep -f "bash $META\b" > /dev/null && ! pgrep -f "bash run_compare.sh" > /dev/null; then
    if grep -q '###### run_compare.sh end' results_compare/META.log 2>/dev/null; then
      log "all blocks complete (META.log end marker found). exiting supervisor."
      break
    fi
    log "meta-driver not running; relaunching..."
    nohup bash "$META" > compare.out 2>> compare.err < /dev/null &
    sleep 5
  fi

  # ensure watcher alive
  if ! pgrep -f "bash $WATCHER\b" > /dev/null && ! pgrep -f "bash auto_push_watcher.sh" > /dev/null; then
    log "watcher not running; relaunching..."
    nohup bash "$WATCHER" > /dev/null 2>&1 < /dev/null &
    sleep 2
  fi

  # NOTE: periodic forced push REMOVED.
  # Pushing every 30min uploaded in-flight timeout-only data which is
  # misleading. Now only auto_push_watcher pushes — and only on block DONE
  # markers so each push contains a fully-completed (impl × level) result.

  sleep 60
done

log "###### supervisor exit"
