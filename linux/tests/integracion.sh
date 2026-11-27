#!/bin/sh
# Prueba de integracion de envmond en el host, con el reloj real:
# socat crea un par de pseudoterminales; envmond abre uno y sim_stm32 (que
# hace de STM32) el otro. No usa hardware.
#
# uso: tests/integracion.sh [corta|larga] (desde linux/)
#   VALGRIND=1   ejecuta envmond bajo Valgrind y exige 0 errores y sin fugas
#   DURACION=S   segundos de la prueba larga (por defecto 1800, PI-11)
set -eu

MODO=${1:-corta}
DURACION=${DURACION:-1800}
VALGRIND=${VALGRIND:-0}
AQUI=$(cd "$(dirname "$0")/.." && pwd)
ENVMOND=$AQUI/envmond
SIM=$AQUI/tests/sim_stm32
CREAR=$AQUI/tests/crear_config

W=$(mktemp -d "${TMPDIR:-/tmp}/envmond-integ-XXXXXX")
SOCAT_PID=
ENVMOND_PID=
fallos=0
casos=0

limpiar() {
    [ -n "$ENVMOND_PID" ] && kill "$ENVMOND_PID" 2>/dev/null || true
    [ -n "$SOCAT_PID" ] && kill "$SOCAT_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$W"
}
trap limpiar EXIT INT TERM

ok()    { casos=$((casos + 1)); echo "  ok    $1"; }
falla() { casos=$((casos + 1)); fallos=$((fallos + 1)); echo "  FALLA $1"; }
comprobar() { # descripcion, comando...
    d=$1; shift
    if "$@"; then ok "$d"; else falla "$d"; fi
}
cuenta() { grep -c -- "$1" "$2" 2>/dev/null || true; }

socat pty,raw,echo=0,link="$W/tty-envmond" pty,raw,echo=0,link="$W/tty-stm32" 2>"$W/socat.log" &
SOCAT_PID=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -e "$W/tty-stm32" ] && break; sleep 0.2; done
[ -e "$W/tty-stm32" ] || { echo "socat no creo los pseudoterminales"; exit 1; }

reiniciar_dirs() {
    rm -rf "$W/etc" "$W/datos"
    mkdir -p "$W/etc" "$W/datos"
}

arrancar() { # nombre del caso
    LOG=$W/$1.log
    if [ "$VALGRIND" = 1 ]; then
        valgrind --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect \
            --error-exitcode=99 --log-file="$W/$1.vg" \
            "$ENVMOND" --uart "$W/tty-envmond" --config-dir "$W/etc" --data-dir "$W/datos" 2>"$LOG" &
    else
        "$ENVMOND" --uart "$W/tty-envmond" --config-dir "$W/etc" --data-dir "$W/datos" 2>"$LOG" &
    fi
    ENVMOND_PID=$!
    for _ in $(seq 1 50); do grep -q "abierto\|arranque:" "$LOG" 2>/dev/null && break; sleep 0.2; done
    sleep 0.5
}

detener() { # nombre del caso
    kill -TERM "$ENVMOND_PID"
    st=0
    wait "$ENVMOND_PID" || st=$?
    ENVMOND_PID=
    comprobar "$1: envmond termina con codigo 0 tras SIGTERM" [ "$st" -eq 0 ]
    if [ "$VALGRIND" = 1 ]; then
        comprobar "$1: Valgrind sin errores" grep -q "ERROR SUMMARY: 0 errors" "$W/$1.vg"
        comprobar "$1: Valgrind sin fugas definitivas ni indirectas" \
            sh -c "! grep -E '(definitely|indirectly) lost: [1-9]' '$W/$1.vg'"
    fi
}

sim() { # nombre, ordenes...
    n=$1; shift
    "$SIM" "$W/tty-stm32" "$@" > "$W/$n.rx"
}

if [ "$MODO" = larga ]; then
    echo "== Prueba larga: $DURACION s con tramas validas, fallas del sensor y tramas malas =="
    reiniciar_dirs
    "$CREAR" "$W/etc"
    arrancar larga
    sim larga aleatorio "$DURACION"
    comprobar "larga: SYS recibido cada segundo (>= 90 %)" \
        [ "$(cuenta 'RX SYS,' "$W/larga.rx")" -ge $((DURACION * 9 / 10)) ]
    detener larga
    grep "fin:" "$W/larga.log" | sed 's/^/  /'
else
    echo "== A. Bring-up: sin configuracion =="
    reiniciar_dirs
    arrancar a
    sim a env 20 40 2 espera 1
    comprobar "A: envia SYS,0 cada segundo" [ "$(cuenta 'RX SYS,0' "$W/a.rx")" -ge 5 ]
    comprobar "A: nunca envia SYS,1 ni SYS,2" [ "$(cuenta 'RX SYS,[12]' "$W/a.rx")" -eq 0 ]
    comprobar "A: todas las tramas recibidas tienen checksum valido" [ "$(cuenta 'RX MALA' "$W/a.rx")" -eq 0 ]
    detener a
    comprobar "A: sin historial en bring-up" [ -z "$(ls "$W/datos")" ]
    comprobar "A: cuenta las 2 ENV" grep -q "fin: tramas_ok=2 " "$W/a.log"

    echo "== B. Normal, errores de trama y alarma tras 5 lecturas =="
    reiniciar_dirs
    "$CREAR" "$W/etc"
    arrancar b
    sim b sta env 20 40 2 basura env 30 40 5 espera 2
    comprobar "B: SYS,1 con lecturas en rango" grep -q "RX SYS,1" "$W/b.rx"
    comprobar "B: SYS,2 tras la quinta lectura fuera de rango" grep -q "RX SYS,2" "$W/b.rx"
    # La alarma no puede aparecer antes de ~12 s de lecturas fuera de rango (4 x 3 s).
    t_alarma=$(awk '/RX SYS,2/ {print $1; exit}' "$W/b.rx")
    comprobar "B: la alarma aparece despues de 5 ENV (t >= 18 s)" [ "${t_alarma:-0}" -ge 18000 ]
    detener b
    comprobar "B: contadores de error (1 checksum, 1 desconocida)" \
        grep -q "tramas_ok=8 err_checksum=1 err_trama=0 desconocidas=1" "$W/b.log"
    # Las 7 ENV validas pueden repartirse en dos intervalos de 60 s.
    validas=$(awk -F, 'FNR > 1 {s += $3} END {print s + 0}' "$W"/datos/*.csv)
    comprobar "B: historial con las 7 lecturas validas al detener" [ "$validas" -eq 7 ]

    echo "== C. Factory reset por BTN =="
    arrancar c
    sim c env 20 40 1 btn espera 4
    comprobar "C: responde ACK,RST" grep -q "RX ACK,RST" "$W/c.rx"
    comprobar "C: SYS,3 durante ~2 s (>= 2 tramas)" [ "$(cuenta 'RX SYS,3' "$W/c.rx")" -ge 2 ]
    comprobar "C: despues vuelve a SYS,0 (bring-up)" \
        [ "$(awk '/RX SYS,3/ {v=1} v && /RX SYS,0/ {print "si"; exit}' "$W/c.rx")" = si ]
    comprobar "C: ACK antes del primer SYS,3" \
        [ "$(awk '/RX ACK,RST/ {print "si"; exit} /RX SYS,3/ {print "no"; exit}' "$W/c.rx")" = si ]
    comprobar "C: config borrada" [ ! -e "$W/etc/config" ]
    comprobar "C: historial borrado" [ -z "$(ls "$W/datos")" ]
    comprobar "C: marcador retirado" [ ! -e "$W/etc/reset.pending" ]
    detener c

    echo "== D. Arranque con reset.pending (corte durante un reset) =="
    reiniciar_dirs
    "$CREAR" "$W/etc"
    printf 'x\n' > "$W/datos/2026-11-26.csv"
    : > "$W/etc/reset.pending"
    arrancar d
    sim d espera 2
    comprobar "D: el primer latido ya es SYS,0" [ "$(awk '/RX SYS,/ {print $3; exit}' "$W/d.rx")" = "SYS,0" ]
    comprobar "D: config, historial y marcador borrados" \
        sh -c "[ ! -e '$W/etc/config' ] && [ ! -e '$W/etc/reset.pending' ] && [ -z \"\$(ls '$W/datos')\" ]"
    comprobar "D: lo registra en el log" grep -q "se completa el reset" "$W/d.log"
    detener d

    echo "== E. Configuracion corrupta: bring-up sin borrarla =="
    reiniciar_dirs
    printf 'nombre=x\n' > "$W/etc/config"
    arrancar e
    sim e espera 2
    comprobar "E: SYS,0" grep -q "RX SYS,0" "$W/e.rx"
    comprobar "E: el archivo se conserva" [ -e "$W/etc/config" ]
    comprobar "E: lo registra en el log" grep -q "configuracion invalida" "$W/e.log"
    detener e
fi

echo "== $((casos - fallos))/$casos comprobaciones correctas =="
[ "$fallos" -eq 0 ]
