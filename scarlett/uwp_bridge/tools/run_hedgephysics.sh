#!/usr/bin/env bash
# run_hedgephysics.sh -- run HedgePhysics.exe under the local Proton build with the
# scarlett UWP bridge and full DXVK logging.
#
# This is the "control command" from scarlett/uwp_bridge/HEDGEPHYSICS_STATUS.md
# section 4, wrapped in a reusable script.
#
# Usage:
#   ./run_hedgephysics.sh [timeout_seconds] [log_path]
#
#   $1  timeout in seconds (default 25; use ~90 for a first run that must also
#       finish Wine prefix creation, although /tmp/uwp_pfx_hedgephysics normally
#       already exists and is reused)
#   $2  combined proton+wine stdout/stderr log (default /tmp/uwp_logs/hp_run_latest.log)
#
# Environment used for the run:
#   STEAM_COMPAT_DATA_PATH=/tmp/uwp_pfx_hedgephysics   Wine prefix (persistent)
#   STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp
#   WINEDEBUG=+debugstr,+err,+uwp,+dxgi                needed for the
#       "[scarlett:uwp:*]" OutputDebugStringA lines and for wine err/uwp lines
#   PROTON_UWP_CORECLR_PATH=Z:\tmp\uwp_coreruntime_1.1\unpacked\coreclr.dll
#       (CoreCLR 1.1 unpacked from Microsoft.NET.CoreRuntime.1.1.appx)
#   DXVK_LOG_LEVEL=info       REQUIRED: Proton's init_session() does
#       env.setdefault("DXVK_LOG_LEVEL", "none"), which silently disables all
#       DXVK logging (including "DxgiSwapChain::Present: frame N") unless we set it.
#   DXVK_LOG_PATH=/tmp/uwp_logs
#       Forces DXVK's Logger::getFileName() to open its per-DLL files instead of
#       only emitting to wine's console output: /tmp/uwp_logs/HedgePhysics_dxgi.log
#       and /tmp/uwp_logs/HedgePhysics_d3d11.log. DXVK also mirrors the same
#       lines to stderr (__wine_dbg_output), so the combined log above captures
#       them too -- both destinations are checked by the summary below.
#
# Notes learned from a verified plumbing run (2026-11-23):
#  * Proton's prefix setup re-copies the DXVK PE DLLs from
#    build/dist/files/lib/wine/dxvk/x86_64-windows/ into the prefix system32 on
#    every run (try_copy = unlink+copy), so a rebuilt DXVK needs no prefix
#    recreation. The scarlett/wine DLLs UWPHOST.dll and
#    windows.devices.enumeration.dll are absolute symlinks into the dist dir.
#  * DXVK_LOG_PATH=/tmp/uwp_logs works as a Unix path under Wine; the per-process
#    files are /tmp/uwp_logs/<exe>_dxgi.log, <exe>_d3d11.log (and xalia_dxgi.log
#    for Proton's xalia helper). They are overwritten on every run, so this
#    script keeps per-run copies next to $LOG as
#    "$LOG.HedgePhysics_dxgi.log" / "$LOG.HedgePhysics_d3d11.log".
#  * `timeout` kills the proton supervisor, but wine still drains for ~5-20 s and
#    keeps appending to the log, so the summary below waits for the log to stop
#    growing before counting anything.
#
# Exit status: the proton/timeout exit status (124 = killed by the timeout).
# Logs are never deleted by this script.

set -u

TIMEOUT="${1:-25}"
LOG="${2:-/tmp/uwp_logs/hp_run_latest.log}"

PROTON=/home/kajitsy/Documents/GitHub/Proton/build/dist/proton
GAME=/mnt/files/Downloads/HedgePhysics_1.2.3.0/HedgePhysics_1.2.3.0_x64/HedgePhysics_1.2.3.0_x64/HedgePhysics.exe
PFX=/tmp/uwp_pfx_hedgephysics
CORECLR='Z:\tmp\uwp_coreruntime_1.1\unpacked\coreclr.dll'
DXVK_LOGDIR=/tmp/uwp_logs

mkdir -p "$DXVK_LOGDIR"
LOG_DIR=$(dirname -- "$LOG")
[ -d "$LOG_DIR" ] && : || mkdir -p "$LOG_DIR"

cmd=(env
  "STEAM_COMPAT_DATA_PATH=$PFX"
  "STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp"
  "WINEDEBUG=+debugstr,+err,+uwp,+dxgi"
  "PROTON_UWP_CORECLR_PATH=$CORECLR"
  "DXVK_LOG_LEVEL=info"
  "DXVK_LOG_PATH=$DXVK_LOGDIR"
  timeout "${TIMEOUT}s" "$PROTON" run "$GAME")

echo "== run_hedgephysics.sh =="
printf 'CMD:'
printf ' %q' "${cmd[@]}"
printf '\n'
echo "LOG:  $LOG"
echo "PREFIX: $PFX ($(du -sh "$PFX" 2>/dev/null | cut -f1 || echo missing))"
echo "START: $(date -Is)"

"${cmd[@]}" >"$LOG" 2>&1
rc=$?

echo "END:   $(date -Is) rc=$rc"

# wine keeps writing to the log for a while after the supervisor is killed --
# wait until the size stops changing (max 40 s) so the summary is complete.
echo -n "waiting for log to settle"
prev_size=-1
for _ in $(seq 1 40); do
  sleep 1
  cur_size=$(stat -c %s "$LOG" 2>/dev/null || echo 0)
  if [ "$cur_size" = "$prev_size" ]; then break; fi
  prev_size=$cur_size
  printf '.'
done
echo " ($(stat -c %s "$LOG" 2>/dev/null || echo 0) bytes)"

count() { grep -ac -- "$1" "$LOG" 2>/dev/null || true; }
show1() { grep -am1 -- "$1" "$LOG" 2>/dev/null | cut -c1-260 || true; }

echo
echo "== quick summary: $LOG ($(stat -c %s "$LOG" 2>/dev/null || echo 0) bytes) =="
echo "-- lifecycle (UWPHOST) --"
for marker in \
  "UwpHostInitialize complete" \
  "IFrameworkView::SetWindow succeeded" \
  "IFrameworkView::Load succeeded" \
  "Firing CoreApplicationView::Activated" \
  "Entering IFrameworkView::Run()" \
  "NotifySwapChainCreated"; do
  n=$(count "$marker")
  printf '  %-40s %s\n' "$marker" "$n"
  if [ "$n" != "0" ]; then show1 "$marker" | sed 's/^/    /'; fi
done

echo "-- DXVK --"
echo "  banner: $(show1 'info:  DXVK:')"
echo "  GPU:    $(show1 'Found device:')"
echo "  swapchain: $(show1 'DXGI: CreateSwapChainForCoreWindow')"
present_log=$(count "DxgiSwapChain::Present: frame")
echo "  Present lines in combined log: $present_log"
shopt -s nullglob
dxvk_files=("$DXVK_LOGDIR"/HedgePhysics_*.log)
shopt -u nullglob
if [ "${#dxvk_files[@]}" -eq 0 ]; then
  echo "  DXVK file logs: none in $DXVK_LOGDIR (DXVK_LOG_PATH ignored? combined stderr still has the lines)"
else
  for f in "${dxvk_files[@]}"; do
    n=$(grep -ac "DxgiSwapChain::Present: frame" "$f" 2>/dev/null || true)
    echo "  DXVK file log: $f ($(stat -c %s "$f") bytes, Present lines: $n)"
    # archive a per-run copy: DXVK truncates these files at the next run
    cp -f "$f" "$LOG.$(basename "$f")" 2>/dev/null || true
  done
fi
echo "  NOTE: the combined log and the DXVK dxgi file contain the SAME Present"
echo "        lines (DXVK mirrors them to stderr); do not add the two counts."
echo "        Highest Present frame id: $(grep -ao 'Present: frame [0-9]*' "$LOG" 2>/dev/null | tail -n1 | grep -o '[0-9]*$' || echo 0)"

echo "-- UNIMPLEMENTED WinRT classes --"
grep -ao "UNIMPLEMENTED WinRT class='[^']*'" "$LOG" 2>/dev/null | sort | uniq -c | sed 's/^/  /' || true

echo "-- failures / notable stubs (tail) --"
grep -aE "E_NOTIMPL|NOTIMPL|CLASSNOTREG|Unimplemented|UNIMPLEMENTED|Unhandled exception|stub!|raiseNotImplemented|AppThread|abort" "$LOG" 2>/dev/null | tail -n 8 | cut -c1-240 | sed 's/^/  /' || true

echo "-- last 5 non-empty log lines --"
grep -av '^[[:space:]]*$' "$LOG" 2>/dev/null | tail -n 5 | cut -c1-240 | sed 's/^/  /' || true

echo
echo "full log:      $LOG"
echo "per-run DXVK:  $LOG.HedgePhysics_dxgi.log, $LOG.HedgePhysics_d3d11.log (copies)"
echo "Present proof:  grep -a 'DxgiSwapChain::Present: frame ' $LOG | wc -l"
exit "$rc"
