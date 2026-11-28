# shellcheck shell=bash
# CI: `gh api` with retries, shared by scripts/ci/trusted_base.sh,
# find_base.sh and prune_bases.sh. Source, don't run.
#
# A transient failure (a 5xx, a secondary rate limit, a timeout) used to be
# swallowed (`gh api ... 2>/dev/null || true`) and read as "no runs" or "no
# archives", so a PR build silently became a full one (#589). gh_api tries
# a call up to GH_API_TRIES times (default 4), waiting GH_API_DELAY seconds
# (default 5) and twice as long after each failure. A 404 is an answer, not
# a failure: it is not retried and returns GH_API_NOT_FOUND. Any other
# failure that outlasts the retries returns 1, after saying on stderr which
# call failed and what GitHub said. Callers must never read that as empty.

GH_API_TRIES="${GH_API_TRIES:-4}"
GH_API_DELAY="${GH_API_DELAY:-5}"
GH_API_NOT_FOUND=4

# gh_api <gh api arguments...>
# Prints the call's output (all pages of a --paginate) only when it worked.
gh_api() {
  local try out err delay="$GH_API_DELAY" errfile
  errfile="$(mktemp)"
  for ((try = 1; ; try++)); do
    if out="$(gh api "$@" 2>"$errfile")"; then
      rm -f "$errfile"
      [[ -z $out ]] || printf '%s\n' "$out"
      return 0
    fi
    err="$(tr '\n' ' ' <"$errfile")"
    if [[ $err == *"(HTTP 404)"* ]]; then
      rm -f "$errfile"
      return "$GH_API_NOT_FOUND"
    fi
    if ((try >= GH_API_TRIES)); then
      rm -f "$errfile"
      echo "gh api $* failed $try times; GitHub said: ${err:-nothing}" >&2
      return 1
    fi
    echo "gh api $* failed (try $try of $GH_API_TRIES, again in ${delay}s): ${err:-no error text}" >&2
    sleep "$delay"
    delay=$((delay * 2))
  done
}
