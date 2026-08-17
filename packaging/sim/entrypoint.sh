#!/bin/sh
# Container entrypoint for the zerotouch-sim image: show a banner that matches
# whichever binary is being run, then exec it.
#
# The image carries two: zerotouch-sim (the SMS CLI) and zt-gnmi-simd (the
# simulated device's gNMI server). zt-gnmi-simd prints its own banner, so this
# only announces the CLI.

case "$1" in
  zt-gnmi-simd|/usr/local/bin/zt-gnmi-simd)
    # Server: its own banner covers it — go straight through.
    ;;
  *)
    cat <<'BANNER'

   _____               _____                _
  |__  /___ _ __ ___  |_   _|__  _   _  ___| |__
    / // _ \ '__/ _ \   | |/ _ \| | | |/ __| '_ \
   / /|  __/ | | (_) |  | | (_) | |_| | (__| | | |
  /____\___|_|  \___/   |_|\___/ \__,_|\___|_| |_|

  SMS simulator  —  no modem, no ds-server
  your input lands straight at Bridge::on_sms

BANNER
    ;;
esac

exec "$@"
