#!/bin/sh
# Container entrypoint for the zerotouch-sim image: show a SIM banner, then run
# whatever CMD/args were given (the simulator by default; a shell if overridden).

cat <<'BANNER'

   ____ ___ __  __
  / ___|_ _|  \/  |
  \___ \| || |\/| |
   ___) | || |  | |
  |____/___|_|  |_|

  zero-touch offline simulator  —  no modem / ds-server / gRPC
  your input lands straight at Bridge::on_sms

BANNER

exec "$@"
