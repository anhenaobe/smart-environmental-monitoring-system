#!/bin/sh
# READ-ONLY. Run on the Milk-V console after verifying that it is the board.
set -eu
printf '\n== Operating system ==\n'; uname -a; cat /etc/os-release || true
printf '\n== Serial devices ==\n'; ls -l /dev/ttyS* /dev/ttyAMA* /dev/ttyUSB* 2>/dev/null || true
printf '\n== Device tree aliases ==\n'; ls -l /proc/device-tree/aliases/ 2>/dev/null || true
printf '\n== Init / services ==\n'; ps -p 1 -o pid,comm,args || true
printf '\n== Network ==\n'; ip -br addr || true
printf '\n== UART and pinmux tools ==\n'; command -v cvi_pinmux || true; command -v systemctl || true
printf '\nThis script changed no pinmux, GPIO direction, or service.\n'
