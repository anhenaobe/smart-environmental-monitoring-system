#!/bin/sh
# Read-only inventory: no tty opened/configured, no pinmux/service writes.
set -u
section() { printf '\n== %s ==\n' "$1"; }
dt_string() { tr -d '\000' < "$1"; printf '\n'; }
section "Kernel / OS / architecture (compare with historic uCuPViewer)"
uname -a
cat /etc/os-release 2>/dev/null || true
section "Boot console -- NEVER use this port for the STM32"
cat /proc/cmdline || true
cat /sys/class/tty/console/active 2>/dev/null || true
cat /proc/consoles 2>/dev/null || true
section "Serial devices"
ls -l /dev/ttyS* /dev/ttyAMA* /dev/ttyUSB* /dev/ttyACM* /dev/serial/by-id/* 2>/dev/null || true
cat /proc/tty/driver/serial 2>/dev/null || true
section "Device-tree serial aliases (not just file names)"
for prop in /proc/device-tree/aliases/serial* /proc/device-tree/chosen/stdout-path /proc/device-tree/chosen/linux,stdout-path; do
    [ -r "$prop" ] || continue
    printf '%s = ' "$prop"; dt_string "$prop"
done
section "Serial sysfs -> actual controller"
for tty in /sys/class/tty/ttyS* /sys/class/tty/ttyAMA*; do
    [ -e "$tty/device" ] || continue
    printf '\n%s -> ' "$tty"; readlink -f "$tty/device" || true
    if [ -e "$tty/device/of_node" ]; then
        printf '%s -> ' "$tty/device/of_node"; readlink -f "$tty/device/of_node" || true
        if [ -r "$tty/device/of_node/status" ]; then
            printf 'DT status = '; dt_string "$tty/device/of_node/status"
        fi
    fi
done
section "Init and serial getty (do not stop services automatically)"
ps -p 1 -o pid,comm,args 2>/dev/null || true
if command -v systemctl >/dev/null 2>&1; then
    systemctl list-units --type=service --all --no-pager 'serial-getty@*' 2>/dev/null || true
fi
section "Network and build tools"
ip -br addr 2>/dev/null || true
for tool in gcc make cvi_pinmux duo-pinmux fuser lsof; do
    command -v "$tool" 2>/dev/null || true
done
if command -v gcc >/dev/null 2>&1; then
    gcc --version | head -n 1
    gcc -dumpmachine
fi
section "Pinmux (read-only) for UART2 candidate J3 B11/B12"
if command -v duo-pinmux >/dev/null 2>&1; then
    duo-pinmux -r B11 || true
    duo-pinmux -r B12 || true
elif command -v cvi_pinmux >/dev/null 2>&1; then
    # uCuPViewer's actual Debian image had this command; -r reads mux state.
    # Raw aliases are verified from the official CV181x pinlist.
    cvi_pinmux -r VIVO_D10 || true
    cvi_pinmux -r VIVO_D9 || true
else
    printf 'No known pinmux read tool found. Check the installed image documentation.\n'
fi
section "Kernel UART registration (permissions may restrict output)"
dmesg 2>/dev/null | grep -Ei 'ttyS[0-9]|ttyAMA[0-9]|serial|uart' | tail -n 50 || true
printf '\nInventory only. No GPIO, pinmux, tty or service modified. No UART verified.\n'
