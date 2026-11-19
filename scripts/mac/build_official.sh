#!/usr/bin/env bash
# Builds Koan's official (ThinLTO + PGO) Koan.app on a Mac, in its own out
# dir. The design, estimates and risks: .agent/notes/mac.md, "Official build".
#
#   scripts/mac/build_official.sh [options]
#
#   1. checks the Mac: the usual prerequisites (scripts/koan_prereqs.sh), the
#      disk an official build needs, the RAM, the Xcode SDK;
#   2. takes the Mac's build/test lock ($KOAN_MAC/tmp/e2e.lock, the same
#      flock(2) lock as the Mac loop's stand-in; scripts/mac/tools/flock_run.py)
#      and holds it to the end;
#   3. fetches Chromium and applies Koan's patches and branding when needed
#      (scripts/build.sh --sync-only, the same steps as the everyday build);
#   4. makes sure Chromium's mac-arm PGO profile is there: sets
#      checkout_pgo_profiles in .gclient (so later syncs keep it current) and
#      runs only that one gclient hook (needs the network once, ~100s of MB);
#   5. writes chromium/src/out/KoanOfficial/args.gn from
#      build/koan-mac-official.gn plus the machine's lines, runs gn gen, and
#      checks the generated build: one link at a time, the ThinLTO thread cap
#      on the framework's link, the PGO profile on Koan's objects;
#   6. builds `chrome` under caffeinate, nice -n 19 and (unless --away)
#      taskpolicy -b, pausing the whole build while the load stays high;
#   7. VMP-signs it for Widevine when this Mac has an EVS login, then signs it
#      with the local identity (scripts/mac_sign.sh, KOAN_SIGNING_KEYCHAIN);
#   8. prints out/KoanOfficial/Koan.app and the next steps (benchmark, and how
#      to install it as the daily app, which this script never does).
#
#   Never touches out/Koan or ~/Applications. Every step is skipped when done,
#   so after an interruption (Ctrl-C, a closed lid, no network) run it again:
#   siso resumes, and the ThinLTO cache keeps a link's finished modules.
#
# House rules for the maintainer's Air (fanless, 16 GB; .agent/notes/mac.md):
#   default    -j 4 at most, nice -n 19, taskpolicy -b (background QoS);
#              pauses while the 1-minute load stays above 8
#   --away     only when the user has said they're away: -j 8, no
#              taskpolicy -b; pauses above load 16
#
# Options:
#   --away        the user is away (above)
#   --check       only the checks of step 1
#   --dry-run     print the plan, change nothing (runs on Linux too, with
#                 KOAN_FAKE_* as in scripts/build.sh; CI tests it)
#   --gen         steps 1-5 only
#   --no-sync     skip step 3 (chromium/src is already as you want it)
#   --no-sign     skip step 7
#
# Environment:
#   JOBS                compile jobs (capped at 4 without --away)
#   KOAN_THIN_LTO_JOBS  ThinLTO threads in one link (default from the RAM:
#                       4 up to 16 GB, 6 up to 24, 8 up to 32, else all)
#   KOAN_PGO=0          build without the PGO profile (slower browser)
#   KOAN_GN_EXTRA       more args.gn lines (only ones the file doesn't set)
#   KOAN_PAUSE_LOAD     load that pauses the build (8, or 16 with --away)
#   KOAN_BUILD_LOCK     the lock file (default $KOAN_MAC/tmp/e2e.lock);
#   KOAN_LOCK_WAIT=0    fail at once instead of waiting for it
#   KOAN_SIGN=0         same as --no-sign
#
# Bash 3.2 (macOS's) compatible.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_prereqs.sh
source "$ROOT/scripts/koan_prereqs.sh"

OUT=out/KoanOfficial
ARGS_FILE="$ROOT/build/koan-mac-official.gn"
SRC="$ROOT/chromium/src"
STATE="$ROOT/.cache/build-state"
# The DEPS hook "Fetch PGO profiles for mac arm", run on its own.
PGO_TARGET=mac-arm
PGO_BUCKET=chromium-optimization-profiles/pgo_profiles

# Disk for a first official build, in GB (estimates; .agent/notes/mac.md):
# objects (ThinLTO bitcode, no symbols) 25-35, ThinLTO cache <= 10% of the
# free disk, link temporaries 2-5, the app and unstripped copies ~2, the
# profile < 1, swap growth during the link 2-8. What the out dir already
# holds counts towards it, down to the minimum for a relink.
KOAN_OFFICIAL_DISK_FRESH_GB=70
KOAN_OFFICIAL_DISK_MIN_GB=25
KOAN_OFFICIAL_MIN_RAM_GB=12
KOAN_OFFICIAL_GOOD_RAM_GB=24

# --- judges: print a line, return 1 on FAIL (scripts/tests/test_build_official.py)

koan_judge_official_disk() { # <free GB> <GB already in the out dir>
  local free=$1 have=${2:-0} need what="a first official build"
  need=$((KOAN_OFFICIAL_DISK_FRESH_GB - have))
  ((need >= KOAN_OFFICIAL_DISK_MIN_GB)) || need=$KOAN_OFFICIAL_DISK_MIN_GB
  ((have < 5)) || what="the rest of the official build"
  if ((free < need)); then
    echo "FAIL  Disk: $free GB free, $what needs about $need GB" \
      "(ThinLTO objects and cache). Free some space, or delete" \
      "chromium/src/$OUT/thinlto-cache (it is only a cache)."
    return 1
  elif ((free < need + 30)); then
    echo "warn  Disk: $free GB free; $what needs about $need GB, so little" \
      "is left for macOS's swap during the link."
  else
    echo "ok    Disk: $free GB free ($what needs about $need GB)"
  fi
}

koan_judge_official_ram() { # <GB of RAM>
  if (($1 < KOAN_OFFICIAL_MIN_RAM_GB)); then
    echo "FAIL  RAM: $1 GB. The ThinLTO link of the framework needs at least" \
      "$KOAN_OFFICIAL_MIN_RAM_GB GB."
    return 1
  elif (($1 < KOAN_OFFICIAL_GOOD_RAM_GB)); then
    echo "warn  RAM: $1 GB. The framework's ThinLTO link (the last ~1-2 h)" \
      "is capped at $(koan_thin_lto_jobs_for_ram "$1") threads; close big" \
      "apps while it runs."
  else
    echo "ok    RAM: $1 GB"
  fi
}

koan_judge_sdk() { # <SDK version, or ""> <Chromium's mac_sdk_official_version>
  if [[ -z $1 ]]; then
    echo "FAIL  macOS SDK: xcrun finds none in DEVELOPER_DIR=${DEVELOPER_DIR:-}."
    return 1
  elif [[ $1 != "$2" && $1 != "$2".* ]]; then
    echo "warn  macOS SDK $1: Chromium's official builds use $2 (Koan isn't" \
      "Chrome-branded, so it builds, but this SDK is untested)."
  else
    echo "ok    macOS SDK $1"
  fi
}

# --- pure helpers (tested)

# ThinLTO backend threads for one link, from the total RAM. Fixed by the
# machine, not by --away, so switching modes never relinks.
koan_thin_lto_jobs_for_ram() { # <GB>
  if (($1 <= 16)); then echo 4
  elif (($1 <= 24)); then echo 6
  elif (($1 <= 32)); then echo 8
  else echo 0  # Chromium's own -Wl,--thinlto-jobs=all
  fi
}

# Compile jobs: <away 0|1> <CPUs> <GB available now> [JOBS asked for].
koan_official_jobs() {
  local away=$1 cpus=$2 avail=$3 asked=${4:-} jobs cap
  if ((away == 1)); then cap=8; else cap=4; fi
  ((cap <= cpus)) || cap=$cpus
  if [[ -n $asked ]]; then
    jobs=$asked
  elif ((away == 1)); then
    if ((avail >= 10)); then jobs=8; elif ((avail >= 6)); then jobs=6; else jobs=4; fi
  else
    if ((avail >= 6)); then jobs=4; elif ((avail >= 4)); then jobs=3; else jobs=2; fi
  fi
  ((jobs <= cap)) || jobs=$cap
  ((jobs >= 1)) || jobs=1
  echo "$jobs"
}

# One step of the load watchdog. Prints "<paused> <high> <low> <action>",
# action being stop, cont or -. Pauses after 4 samples in a row (2 min at
# 30 s) above <pause>, resumes after 2 in a row below <resume>.
koan_pause_step() { # <paused 0|1> <load> <pause at> <resume below> <high> <low>
  awk -v p="$1" -v l="$2" -v hi_at="$3" -v lo_at="$4" -v h="$5" -v o="$6" 'BEGIN {
    a = "-"
    if (p == 0) {
      h = (l > hi_at) ? h + 1 : 0; o = 0
      if (h >= 4) { p = 1; h = 0; a = "stop" }
    } else {
      o = (l < lo_at) ? o + 1 : 0; h = 0
      if (o >= 2) { p = 0; o = 0; a = "cont" }
    }
    print p, h, o, a
  }'
}

# The last --thinlto-jobs value on a link line.
koan_last_thinlto_jobs() { # <ldflags line>
  grep -o -- '--thinlto-jobs=[^ "]*' <<<"$1" | tail -n 1 | sed 's/.*=//'
}

# Rough wall time of a first build, for the log (estimates;
# .agent/notes/mac.md, "Expected results").
koan_official_estimate() { # <away 0|1>
  if (($1 == 1)); then
    echo "about 10-16 h for a first build at -j 8 (estimate), the framework's ThinLTO link alone 1-2 h"
  else
    echo "about 24-40 h for a first build at -j 4 on the efficiency cores (estimate); use --away overnight"
  fi
}

# --- probes (KOAN_FAKE_* for the tests and --dry-run on Linux)

probe_sdk() {
  if [[ -n ${KOAN_FAKE_SDK+x} ]]; then echo "$KOAN_FAKE_SDK"; return; fi
  env -u CFFIXED_USER_HOME xcrun --sdk macosx --show-sdk-version 2>/dev/null || true
}
probe_cpus() {
  if [[ -n ${KOAN_FAKE_CPUS:-} ]]; then echo "$KOAN_FAKE_CPUS"; return; fi
  if [[ $(uname -s) == Darwin ]]; then sysctl -n hw.ncpu; else nproc; fi
}
probe_avail_gb() {
  if [[ -n ${KOAN_FAKE_AVAIL_GB:-} ]]; then echo "$KOAN_FAKE_AVAIL_GB"; return; fi
  koan_available_gb
}
probe_load() { sysctl -n vm.loadavg | awk '{ print $2 }'; }
sdk_official_version() {
  local v=""
  if [[ -f $SRC/build/config/mac/mac_sdk.gni ]]; then
    v="$(sed -n 's/^ *mac_sdk_official_version = "\(.*\)"/\1/p' \
      "$SRC/build/config/mac/mac_sdk.gni" | head -n 1)"
  fi
  echo "${v:-27.0}"
}
pgo_profile_name() {
  [[ -f $SRC/chrome/build/$PGO_TARGET.pgo.txt ]] || return 0
  tr -d '[:space:]' < "$SRC/chrome/build/$PGO_TARGET.pgo.txt"
}
pgo_profile_path() {
  local name
  name="$(pgo_profile_name)"
  if [[ -n $name ]]; then echo "$SRC/chrome/build/pgo_profiles/$name"; fi
}

step() { echo "==> $*"; }

# GB already in the out dir.
probe_out_gb() {
  if [[ -n ${KOAN_FAKE_OUT_GB:-} ]]; then echo "$KOAN_FAKE_OUT_GB"; return; fi
  if [[ -d $SRC/$OUT ]]; then
    du -sk "$SRC/$OUT" 2>/dev/null | awk '{ print int($1 / 1048576) }'
  else
    echo 0
  fi
}

check_machine() {
  local rc=0
  koan_check_prereqs "$ROOT" || rc=1
  koan_judge_official_disk "$(koan_probe_disk_gb "$ROOT")" "$(probe_out_gb)" || rc=1
  koan_judge_official_ram "$(koan_probe_ram_gb)" || rc=1
  koan_judge_sdk "$(probe_sdk)" "$(sdk_official_version)" || rc=1
  return "$rc"
}

# Step 4. Sets checkout_pgo_profiles in .gclient, so update_chromium.sh's
# syncs keep the profile in step with each new Chromium, and runs only the
# DEPS hook that fetches it now (not every hook, as gclient runhooks would).
ensure_pgo_profile() {
  local profile gclient="$ROOT/chromium/.gclient"
  if [[ ${KOAN_PGO:-1} == 0 ]]; then
    step "PGO off (KOAN_PGO=0): the build won't use Chromium's profile"
    return
  fi
  profile="$(pgo_profile_path)"
  [[ -n $profile ]] || { echo "no chrome/build/$PGO_TARGET.pgo.txt in chromium/src" >&2; exit 1; }
  # Break loudly if upstream renamed the hook's target or bucket.
  if ! grep -q -- "--target=$PGO_TARGET'" "$SRC/DEPS" ||
      ! grep -q -- "--gs-url-base=$PGO_BUCKET'" "$SRC/DEPS"; then
    echo "chromium/src/DEPS no longer has the '$PGO_TARGET' PGO hook with" \
      "$PGO_BUCKET: update PGO_TARGET/PGO_BUCKET in $0" >&2
    exit 1
  fi
  python3 -I - "$gclient" <<'EOF'
import pprint
import shutil
import sys

path = sys.argv[1]
with open(path) as f:
  text = f.read()
scope = {}
exec(compile(text, path, 'exec'), scope)
solutions = scope['solutions']
src = [s for s in solutions if s.get('name') == 'src']
if not src:
  sys.exit(f'{path} has no "src" solution')
custom_vars = src[0].setdefault('custom_vars', {})
if custom_vars.get('checkout_pgo_profiles') is True:
  sys.exit(0)
custom_vars['checkout_pgo_profiles'] = True
shutil.copyfile(path, path + '.before-pgo')
with open(path, 'w') as f:
  for key, value in scope.items():
    if not key.startswith('__'):
      f.write(f'{key} = {pprint.pformat(value)}\n')
print(f'set checkout_pgo_profiles in {path} (old copy: {path}.before-pgo)')
EOF
  if [[ -f $profile ]]; then
    step "PGO profile: $(basename "$profile")"
    return
  fi
  step "Fetching Chromium's $PGO_TARGET PGO profile (needs the network once)"
  # The hook's own command (DEPS), with the python3 gclient would run it with.
  if ! (cd "$SRC" && python3 tools/update_pgo_profiles.py \
      --target="$PGO_TARGET" update --gs-url-base="$PGO_BUCKET"); then
    echo "Couldn't fetch gs://$PGO_BUCKET/$(basename "$profile") (it needs" \
      "access to storage.googleapis.com). Connect and run this again; it" \
      "resumes. KOAN_PGO=0 builds without it (a slower browser)." >&2
    exit 1
  fi
  [[ -f $profile ]] || { echo "the hook ran but $profile is missing" >&2; exit 1; }
  echo "PGO profile: $profile ($(du -h "$profile" | cut -f1))"
}

# Step 5.
write_args() {
  local new metal lto_jobs pgo=2
  [[ ${KOAN_PGO:-1} != 0 ]] || pgo=0
  lto_jobs="${KOAN_THIN_LTO_JOBS:-$(koan_thin_lto_jobs_for_ram "$(koan_probe_ram_gb)")}"
  mkdir -p "$SRC/$OUT"
  new="$SRC/$OUT/args.gn.new"
  cp "$ARGS_FILE" "$new"
  # ANGLE runs the Metal toolchain directly (see write_args in build.sh).
  metal="$(env -u CFFIXED_USER_HOME xcrun -f metal 2>/dev/null || true)"
  [[ -x $metal ]] || { echo "no Metal toolchain: run xcodebuild -downloadComponent MetalToolchain" >&2; exit 1; }
  ln -sfn "$(dirname "$metal")" "$SRC/$OUT/metal-toolchain"
  {
    echo
    echo "# --- written by scripts/mac/build_official.sh ---"
    echo "chrome_pgo_phase = $pgo"
    echo "koan_thin_lto_jobs = $lto_jobs"
    echo "angle_metal_toolchain_dir = \"$SRC/$OUT/metal-toolchain\""
    # A way out without editing the checked-in file, e.g.
    # KOAN_GN_EXTRA='fatal_linker_warnings = false' (relinks, no recompile).
    if [[ -n ${KOAN_GN_EXTRA:-} ]]; then printf '%s\n' "$KOAN_GN_EXTRA"; fi
  } >> "$new"
  # Only rewrite args.gn when it changes, so a rerun regenerates nothing.
  if cmp -s "$new" "$SRC/$OUT/args.gn"; then rm "$new"; else mv "$new" "$SRC/$OUT/args.gn"; fi
}

# After gn gen: the build is what the design says, or stop before hours of
# compiling. KOAN_SKIP_GEN_CHECKS=1 skips this (say why in an issue).
check_generated() {
  local depth lto_jobs ld last ram
  [[ ${KOAN_SKIP_GEN_CHECKS:-0} == 0 ]] || return 0
  ram="$(koan_probe_ram_gb)"
  lto_jobs="${KOAN_THIN_LTO_JOBS:-$(koan_thin_lto_jobs_for_ram "$ram")}"
  depth="$(awk '/^pool build_toolchain_link_pool/ { getline; print $3; exit }' \
    "$SRC/$OUT/build.ninja")"
  echo "links at a time: ${depth:-?} (Chromium's concurrent_links for ThinLTO on $ram GB)"
  if ((ram <= 24)) && [[ $depth != 1 ]]; then
    echo "expected one link at a time on $ram GB, got '${depth:-none}'" >&2
    exit 1
  fi
  ld="$(grep -h '^ *ldflags = .*-flto=thin' "$SRC/$OUT"/obj/chrome/chrome_framework*.ninja 2>/dev/null |
    head -n 1 || true)"
  if [[ -z $ld ]]; then
    echo "no ThinLTO link line for the framework in $OUT/obj/chrome: is" \
      "is_official_build on?" >&2
    exit 1
  fi
  last="$(koan_last_thinlto_jobs "$ld")"
  if ((lto_jobs > 0)) && [[ $last != "$lto_jobs" ]]; then
    echo "the framework links with --thinlto-jobs=$last, not $lto_jobs:" \
      "//koan/build:thin_lto_jobs no longer caps it (upstream moved the" \
      "flag?). See .agent/notes/mac.md, \"Official build\"." >&2
    exit 1
  fi
  echo "ThinLTO threads in the framework's link: $last"
  if [[ ${KOAN_PGO:-1} != 0 ]] &&
      ! grep -q -- '-fprofile-use=' "$SRC/$OUT/obj/koan/browser/feature_overrides.ninja"; then
    echo "Koan's objects don't get -fprofile-use: PGO is off in $OUT" >&2
    exit 1
  fi
}

# Step 6 helpers. The build runs in a process group of its own (set -m), so
# the watchdog can stop and continue every compiler in it at once.
BUILD_PID=""
cont_build() {
  if [[ -n $BUILD_PID ]]; then kill -CONT -- "-$BUILD_PID" 2>/dev/null || true; fi
}
on_signal() {
  echo "interrupted: stopping the build (run this again to resume)" >&2
  if [[ -n $BUILD_PID ]]; then
    cont_build
    kill -INT -- "-$BUILD_PID" 2>/dev/null || true
    wait "$BUILD_PID" 2>/dev/null || true
  fi
  exit 130
}

watch_load() { # <build pid> <pause at> <resume below>
  local pid=$1 at=$2 below=$3 paused=0 high=0 low=0 action load n=0 r
  while kill -0 "$pid" 2>/dev/null; do
    sleep 30
    load="$(probe_load)"
    r="$(koan_pause_step "$paused" "$load" "$at" "$below" "$high" "$low")"
    read -r paused high low action <<<"$r"
    case $action in
      stop)
        echo "[watchdog $(date +%H:%M)] load $load stayed above $at: pausing the build"
        kill -STOP -- "-$pid" 2>/dev/null || true ;;
      cont)
        echo "[watchdog $(date +%H:%M)] load $load: resuming the build"
        kill -CONT -- "-$pid" 2>/dev/null || true ;;
    esac
    # Every 10 min: load, free memory and swap, for the next estimate.
    n=$((n + 1))
    if ((n % 20 == 0)); then
      echo "[watchdog $(date +%H:%M)] load $load, $(koan_available_gb) GB free," \
        "$(sysctl -n vm.swapusage | sed 's/.*used = \([^ ]*\).*/swap used \1/')"
    fi
  done
}

run_build() { # <jobs> <away>
  local jobs=$1 away=$2 at below start rc=0
  at="${KOAN_PAUSE_LOAD:-$([[ $away == 1 ]] && echo 16 || echo 8)}"
  below="$(awk -v a="$at" 'BEGIN { print a * 0.6 }')"
  local cmd=(caffeinate -i)
  [[ $away == 1 ]] || cmd+=(taskpolicy -b)
  cmd+=(nice -n 19 autoninja -C "$OUT" -j "$jobs" chrome)
  step "Building: ${cmd[*]}"
  echo "Estimate: $(koan_official_estimate "$away")."
  if [[ -f $STATE/official-build-seconds ]]; then
    echo "The last full official build here took $(( $(cat "$STATE/official-build-seconds") / 60 )) min."
  fi
  echo "The watchdog pauses the build while the 1-minute load stays above $at."
  start="$(date +%s)"
  trap on_signal INT TERM HUP
  set -m
  (cd "$SRC" && exec "${cmd[@]}") &
  BUILD_PID=$!
  set +m
  watch_load "$BUILD_PID" "$at" "$below" &
  local watcher=$!
  # wait can also return when the watchdog stops the build: wait again
  # until it has really exited.
  while :; do
    rc=0
    wait "$BUILD_PID" || rc=$?
    kill -0 "$BUILD_PID" 2>/dev/null || break
  done
  kill "$watcher" 2>/dev/null || true
  cont_build
  BUILD_PID=""
  trap - INT TERM HUP
  echo "build finished in $(( ($(date +%s) - start) / 60 )) min (exit $rc)"
  if ((rc != 0)); then
    echo "The build failed: the first error is above, the log is $LOG." \
      "Run this again to resume after a fix (.agent/notes/mac.md," \
      "\"Official build\", risks)." >&2
    exit "$rc"
  fi
  # Remember a long run (a first or near-full build) for the next estimate.
  if (( $(date +%s) - start > 3 * 3600 )); then
    mkdir -p "$STATE"
    echo $(( $(date +%s) - start )) > "$STATE/official-build-seconds"
  fi
}

app_name() { sed -n 's/^PRODUCT_FULLNAME=//p' "$SRC/chrome/app/theme/chromium/BRANDING"; }

sign_app() { # <app>
  local app=$1
  if [[ $SIGN == 0 ]]; then
    step "Not signing ($([[ ${KOAN_SIGN:-1} == 0 ]] && echo KOAN_SIGN=0 || echo --no-sign))"
    return
  fi
  # Widevine's VMP signature goes into the framework's Resources, which the
  # code signature seals: VMP first (scripts/mac/widevine_vmp_sign.sh,
  # scripts/ci/mac_package.sh).
  "$ROOT/scripts/mac/widevine_vmp_sign.sh" --if-configured "$app"
  "$ROOT/scripts/mac_sign.sh" "$app"
}

next_steps() { # <app>
  local app=$1 name
  name="$(basename "$app" .app)"
  cat <<EOF

Built $app
  $(du -sh "$app" | cut -f1) app; unstripped copies for crash symbols:
$(for f in "$SRC/$OUT"/*.unstripped; do [[ -e $f ]] && echo "    $f"; done)

Next:
  Benchmark (plugged in, other apps quit, 3 runs; compare with Arc the same way):
    "$app/Contents/MacOS/$name" --user-data-dir="\$KOAN_MAC/bench-official" \\
      --use-mock-keychain --no-first-run --no-default-browser-check \\
      'https://browserbench.org/Speedometer3.1/?startAutomatically=true'
    or the Mac loop's bench run, pointed at this app.
  Tests: scripts/mac/suite.sh --skip-cxx "$app"
    (the C++ tests are built in out/Koan, not here)
  Symbolise a crash report: atos -arch arm64 -o "$SRC/$OUT/$name Framework.unstripped" -l <load address> <addresses>

Installing it as your daily Koan only when you ask for it (this script never does):
    quit Koan (⌘Q), then
    ditto "$app" ~/Applications/$name.app.new &&
      mv ~/Applications/$name.app ~/Applications/$name.app.prev &&
      mv ~/Applications/$name.app.new ~/Applications/$name.app
  It is signed with the same local identity, so saved logins keep working;
  ~/Applications/$name.app.prev is the way back.
EOF
}

dry_run() {
  local ram cpus avail jobs lto have_out profile
  ram="$(koan_probe_ram_gb)"; cpus="$(probe_cpus)"; avail="$(probe_avail_gb)"
  jobs="$(koan_official_jobs "$AWAY" "$cpus" "$avail" "${JOBS:-}")"
  lto="${KOAN_THIN_LTO_JOBS:-$(koan_thin_lto_jobs_for_ram "$ram")}"
  have_out="$(probe_out_gb)"
  echo "Koan official build plan (dry run: nothing changes, nothing is fetched)"
  echo "Checks:"
  check_machine | sed 's/^/  /' || true
  echo "  lock         ${KOAN_BUILD_LOCK:-\$KOAN_MAC/tmp/e2e.lock} (flock_run.py)"
  if [[ $SYNC == 1 ]]; then
    echo "  sync         would: scripts/build.sh --sync-only"
  else
    echo "  sync         skip (--no-sync)"
  fi
  profile="$(pgo_profile_path || true)"
  if [[ ${KOAN_PGO:-1} == 0 ]]; then
    echo "  pgo          skip: KOAN_PGO=0 (chrome_pgo_phase = 0)"
  elif [[ -n $profile && -f $profile ]]; then
    echo "  pgo          skip: $(basename "$profile") is there"
  else
    echo "  pgo          would: set checkout_pgo_profiles, run the $PGO_TARGET hook"
  fi
  echo "  args         build/koan-mac-official.gn -> chromium/src/$OUT/args.gn"
  echo "               + chrome_pgo_phase = $([[ ${KOAN_PGO:-1} == 0 ]] && echo 0 || echo 2), koan_thin_lto_jobs = $lto"
  if [[ $AWAY == 1 ]]; then
    echo "  build        would: caffeinate -i nice -n 19 autoninja -C $OUT -j $jobs chrome (--away)"
  else
    echo "  build        would: caffeinate -i taskpolicy -b nice -n 19 autoninja -C $OUT -j $jobs chrome"
  fi
  echo "  pause        while the 1-minute load stays above ${KOAN_PAUSE_LOAD:-$([[ $AWAY == 1 ]] && echo 16 || echo 8)}"
  echo "  estimate     $(koan_official_estimate "$AWAY")$( ((have_out < 5)) || echo "; resumes the out dir ($have_out GB)")"
  if [[ $SIGN == 1 ]]; then
    echo "  sign         would: widevine_vmp_sign.sh --if-configured, then scripts/mac_sign.sh"
  else
    echo "  sign         skip"
  fi
  echo "  install      never (prints how, for when the user asks)"
}

main() {
  local mode=build
  local -a orig
  orig=("$@")
  AWAY=0 SYNC=1 SIGN=1
  [[ ${KOAN_SIGN:-1} != 0 ]] || SIGN=0
  while (($#)); do
    case $1 in
      --away) AWAY=1 ;;
      --check) mode=check ;;
      --dry-run) mode=dry ;;
      --gen) mode=gen ;;
      --no-sync) SYNC=0 ;;
      --no-sign) SIGN=0 ;;
      -h | --help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p;}' "$0"; return 0 ;;
      *) echo "unknown argument $1 ($0 --help)" >&2; return 2 ;;
    esac
    shift
  done

  if [[ $mode == dry ]]; then
    dry_run
    return 0
  fi
  if [[ $(koan_os) != mac ]]; then
    echo "build_official.sh builds on macOS (--dry-run works anywhere)" >&2
    return 2
  fi
  koan_require_env
  export PATH="$ROOT/depot_tools:$PATH"

  if [[ $mode == check ]]; then
    check_machine
    return
  fi

  # Step 2: hold the Mac's build/test lock for the rest of the run.
  if [[ -z ${KOAN_OFFICIAL_LOCKED:-} ]]; then
    export KOAN_OFFICIAL_LOCKED=1
    exec python3 -I "$ROOT/scripts/mac/tools/flock_run.py" \
      "${KOAN_BUILD_LOCK:-$KOAN_MAC/tmp/e2e.lock}" -- \
      /bin/bash "$ROOT/scripts/mac/build_official.sh" ${orig[@]+"${orig[@]}"}
  fi

  mkdir -p "$ROOT/logs" "$STATE"
  LOG="$ROOT/logs/official-$(date +%Y%m%d-%H%M%S).log"
  exec > >(tee "$LOG") 2>&1
  echo "Log: $LOG"
  echo "Xcode: $DEVELOPER_DIR"

  step "Checking the Mac"
  if ! check_machine; then
    echo "Fix the FAIL lines above, then run this again." >&2
    return 1
  fi
  if [[ $SYNC == 1 ]]; then
    "$ROOT/scripts/build.sh" --sync-only
  fi
  ensure_pgo_profile

  local cpus avail jobs
  cpus="$(probe_cpus)"
  avail="$(probe_avail_gb)"
  jobs="$(koan_official_jobs "$AWAY" "$cpus" "$avail" "${JOBS:-}")"
  if [[ -n ${JOBS:-} && $jobs != "$JOBS" ]]; then
    echo "JOBS=$JOBS capped at $jobs ($([[ $AWAY == 1 ]] && echo '--away: 8' || echo 'without --away: 4'))"
  fi
  echo "$jobs compile jobs ($cpus CPUs, $avail GB free now$([[ $AWAY == 1 ]] && echo ', --away'))"

  write_args
  step "gn gen $OUT"
  (cd "$SRC" && gn gen "$OUT")
  check_generated
  if [[ $mode == gen ]]; then
    echo "generated $SRC/$OUT"
    return 0
  fi

  run_build "$jobs" "$AWAY"
  local app
  app="$SRC/$OUT/$(app_name).app"
  [[ -d $app ]] || { echo "the build finished but $app is missing" >&2; return 1; }
  sign_app "$app"
  next_steps "$app"
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  main "$@"
fi
