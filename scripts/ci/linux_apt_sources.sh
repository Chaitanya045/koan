#!/usr/bin/env bash
# Linux CI: copies the apt sources that point at Ubuntu's own archive into a
# directory of their own, for linux_runner_setup.sh's apt-get calls:
#
#   scripts/ci/linux_apt_sources.sh <apt dir> <out dir>
#
# <apt dir> is /etc/apt on a runner (a test passes a fake one). Reads
# <apt dir>/sources.list and <apt dir>/sources.list.d/*.list and *.sources,
# and writes to <out dir> (emptied first) only the files whose every URI is
# on ubuntu.com (archive.ubuntu.com, its azure. and other mirrors,
# security.ubuntu.com, ports.ubuntu.com), following mirror+file: lists.
# Other files are skipped and named on stderr.
#
# Why: the GitHub runner image ships third-party sources (Microsoft's and
# others) the build installs nothing from, and an `apt-get update` over all
# of them fails the job when any one of those servers has a bad minute
# (a 403 from packages.microsoft.com, run 45980089943). Ubuntu's own archive
# failing must still fail the job, so finding no Ubuntu source is an error.
#
# Root isn't needed: it only reads <apt dir>.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <apt dir> <out dir>" >&2
  exit 2
fi
APT_DIR="$1"
OUT="$2"

# The URIs a sources file names: the URL after "deb"/"deb-src" (and its
# [options]) in the one-line format, the URIs: field in deb822 .sources.
# Comments and deb822 stanzas with "Enabled: no" don't count.
uris() {
  case "$1" in
    *.sources)
      awk '
        function flush() { if (enabled) for (i = 1; i <= n; i++) print u[i]; n = 0; enabled = 1 }
        BEGIN { enabled = 1 }
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*$/ { flush(); next }
        tolower($1) == "enabled:" && tolower($2) == "no" { enabled = 0 }
        tolower($1) == "uris:" { for (i = 2; i <= NF; i++) u[++n] = $i }
        END { flush() }
      ' "$1"
      ;;
    *)
      awk '
        /^[[:space:]]*#/ { next }
        $1 == "deb" || $1 == "deb-src" {
          i = 2
          if ($i ~ /^\[/) { while (i <= NF && $i !~ /\]$/) i++; i++ }
          if (i <= NF) print $i
        }
      ' "$1"
      ;;
  esac
}

# Whether a URI's host is ubuntu.com or one of its subdomains. A
# mirror+file:<list> URI (the runner image's ubuntu.sources uses
# mirror+file:/etc/apt/apt-mirrors.txt) counts when every mirror in the list
# does; a list under /etc/apt is read from <apt dir>, so a test's fake works.
ubuntu_uri() {
  local uri="$1"
  uri="${uri#mirror+}"
  if [[ $uri == file:* ]]; then
    local list="${uri#file:}" m n=0
    [[ $list != /etc/apt/* ]] || list="$APT_DIR/${list#/etc/apt/}"
    [[ -f $list ]] || return 1
    while read -r m; do
      ubuntu_uri "$m" || return 1
      n=$((n + 1))
    done < <(awk '!/^[[:space:]]*(#|$)/ { print $1 }' "$list")
    [[ $n -gt 0 ]]
    return
  fi
  local host="${uri#*://}"
  host="${host%%/*}"
  host="${host##*@}"
  host="${host%%:*}"
  [[ $host == ubuntu.com || $host == *.ubuntu.com ]]
}

rm -rf "$OUT"
mkdir -p "$OUT"
kept=0
shopt -s nullglob
for f in "$APT_DIR/sources.list" "$APT_DIR"/sources.list.d/*.list \
    "$APT_DIR"/sources.list.d/*.sources; do
  [[ -f $f ]] || continue
  mapfile -t list < <(uris "$f")
  [[ ${#list[@]} -gt 0 ]] || continue
  ok=1
  for u in "${list[@]}"; do
    ubuntu_uri "$u" || { ok=0; break; }
  done
  if [[ $ok == 1 ]]; then
    name="$(basename "$f")"
    # /etc/apt/sources.list goes into the parts directory too: apt reads
    # only *.list and *.sources there.
    [[ $name != sources.list ]] || name=sources.list.list
    cp "$f" "$OUT/$name"
    kept=$((kept + 1))
  else
    echo "skipping $f (not Ubuntu's archive: ${list[*]})" >&2
  fi
done

if [[ $kept == 0 ]]; then
  echo "no apt source for Ubuntu's archive under $APT_DIR" >&2
  exit 1
fi
