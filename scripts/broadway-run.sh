#!/bin/sh
# Run the Linux build under GTK's Broadway backend, which draws the app in a
# browser tab instead of on a desktop: handy from WSL, or anywhere a window
# on screen would get in the way.
#
# Usage: scripts/broadway-run.sh [display] [seconds] [instances] [-- npad args...]
#   display    Broadway display number (default 45); the page is served on
#              http://localhost:<8080 + display>
#   seconds    how long before everything exits by itself (default 900, min 30)
#   instances  how many npad windows to start on the same session bus
#              (default 1; they see each other as running instances)
#   npad args  passed to every instance, for example a file to open
#
# Environment:
#   NPAD_BIN            the binary to run (default npad-linux in the repo root)
#   NPAD_SETTINGS       a settings.json to seed the throwaway profile with
#   BROADWAY_KEEP_BINS  set to 1 to run the binaries in place instead of
#                       renamed copies
#
# Each run gets a fresh profile under ${TMPDIR:-/tmp}: HOME and the XDG
# directories point there, so your own settings, recovery files and recent
# files are never read or written. The profile, with broadwayd.log and one
# npad-<n>.log per instance, is left behind for you to look at.
#
# It runs its own dbus-run-session, so the instances do not see npads in other
# sessions, and nested timeouts end broadwayd and every npad by themselves:
# nothing needs killing afterwards. broadwayd and npad run as renamed copies in
# the profile, so a `pkill broadwayd` or `pkill npad` elsewhere cannot match
# them (and never use those yourself: they stop other people's sessions too).
#
# A display that is already in use is refused: a second broadwayd on a taken
# port still hands out a socket, and the app then appears in someone else's
# session (LESSONS.md).
set -eu

display=${1:-45}
secs=${2:-900}
count=${3:-1}
if [ $# -gt 3 ]; then
    shift 3
    if [ "$1" = "--" ]; then shift; fi
else
    set --
fi

case "$display" in '' | *[!0-9]*)
    echo "broadway-run: display must be a number, got '$display'" >&2
    exit 2
    ;;
esac
case "$secs" in '' | *[!0-9]*)
    echo "broadway-run: seconds must be a number, got '$secs'" >&2
    exit 2
    ;;
esac
case "$count" in '' | *[!0-9]*)
    echo "broadway-run: instances must be a number, got '$count'" >&2
    exit 2
    ;;
esac
if [ "$secs" -lt 30 ]; then
    echo "broadway-run: seconds must be at least 30" >&2
    exit 2
fi
if [ "$count" -lt 1 ] || [ "$count" -gt 20 ]; then
    echo "broadway-run: instances must be between 1 and 20" >&2
    exit 2
fi
port=$((8080 + display))
if [ "$port" -gt 65535 ]; then
    echo "broadway-run: display $display is too large" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
bin=${NPAD_BIN:-$root/npad-linux}
if [ ! -x "$bin" ]; then
    echo "broadway-run: $bin not found or not executable (run: make linux-gtk)" >&2
    exit 1
fi
for tool in broadwayd dbus-run-session timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "broadway-run: $tool not found (Debian/Ubuntu: libgtk-3-bin, dbus, coreutils)" >&2
        exit 1
    fi
done

# Refuse a display someone is already serving
busy=0
if command -v ss >/dev/null 2>&1; then
    if ss -Hltn "sport = :$port" 2>/dev/null | grep -q .; then busy=1; fi
elif command -v nc >/dev/null 2>&1; then
    if nc -z 127.0.0.1 "$port" 2>/dev/null; then busy=1; fi
else
    echo "broadway-run: neither ss nor nc found, relying on broadwayd's log" >&2
fi
if [ "$busy" -eq 1 ]; then
    echo "broadway-run: port $port (display :$display) is already in use; pick another display" >&2
    exit 1
fi

profile=$(mktemp -d "${TMPDIR:-/tmp}/npad-broadway-$display.XXXXXX")
mkdir -p "$profile/.config/npad" "$profile/.local/share" "$profile/.local/state" "$profile/.cache"
if [ -n "${NPAD_SETTINGS:-}" ]; then
    cp "$NPAD_SETTINGS" "$profile/.config/npad/settings.json"
fi

if [ "${BROADWAY_KEEP_BINS:-0}" = 1 ]; then
    BR_BROADWAYD=$(command -v broadwayd)
    BR_NPAD=$bin
else
    BR_BROADWAYD="$profile/broadwayd-$display"
    BR_NPAD="$profile/npad-broadway-$display"
    cp "$(command -v broadwayd)" "$BR_BROADWAYD"
    cp "$bin" "$BR_NPAD"
fi

export HOME="$profile" XDG_CONFIG_HOME="$profile/.config" XDG_DATA_HOME="$profile/.local/share" \
    XDG_STATE_HOME="$profile/.local/state" XDG_CACHE_HOME="$profile/.cache"
export BR_DISPLAY="$display" BR_SECS="$secs" BR_COUNT="$count" BR_BROADWAYD BR_NPAD

echo "Broadway :$display on http://localhost:$port for ${secs}s, $count instance(s)"
echo "Profile and logs: $profile"

status=0
timeout "$secs" dbus-run-session -- sh -c '
    timeout $((BR_SECS - 10)) "$BR_BROADWAYD" ":$BR_DISPLAY" >"$HOME/broadwayd.log" 2>&1 &
    broadwayd_pid=$!
    sleep 2
    if grep -qi "address already in use" "$HOME/broadwayd.log"; then
        echo "broadway-run: display :$BR_DISPLAY is taken (see $HOME/broadwayd.log)" >&2
        # Our own broadwayd, by its own PID (timeout passes the signal on)
        kill "$broadwayd_pid" 2>/dev/null
        exit 3
    fi
    i=0
    while [ "$i" -lt "$BR_COUNT" ]; do
        GDK_BACKEND=broadway BROADWAY_DISPLAY=":$BR_DISPLAY" \
            timeout $((BR_SECS - 20)) "$BR_NPAD" "$@" >"$HOME/npad-$i.log" 2>&1 &
        i=$((i + 1))
        sleep 2
    done
    wait
' broadway-run "$@" || status=$?

# 124 is timeout ending the run as planned
if [ "$status" -eq 124 ]; then status=0; fi
echo "Broadway :$display finished (status $status). Profile and logs: $profile"
exit "$status"
