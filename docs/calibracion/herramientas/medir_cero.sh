#!/bin/bash
# Medicion del cero con las puntas en corto. Uso: medir_cero.sh <log>
LOG=$1; F=$(mktemp -u); mkfifo $F
cd /home/napoleon/codigo/openhantek/dso5200basic
build/dso5200basic --firmware-dir firmware < $F > $LOG 2>&1 &
PID=$!
exec 3>$F
s(){ echo "$1" >&3; }
mark(){ echo "== $1" >&3; }   # comando desconocido: deja marca en el log
sleep 4
s "timebase 1ms"; s "start"; sleep 2
for v in 10 5 2 1 0.5 0.2 0.1 0.05 0.02 0.01; do
  mark "vdiv $v dc"; s "volts 1 $v"; s "volts 2 $v"; sleep 2
  s "zero 1"; s "zero 2"; s "cal 1"; sleep 1.2; s "cal 2"; sleep 1.2; s "cal off"
done
for v in 5 0.5 0.05; do
  s "volts 1 $v"; s "volts 2 $v"
  for f in 0.25 0.5 0.75; do
    mark "vdiv $v offset $f"; s "offsettest 1 $f"; s "offsettest 2 $f"; sleep 2; s "zero 1"; s "zero 2"
  done
  s "offsettest 1 0.5"; s "offsettest 2 0.5"
  mark "vdiv $v ac"; s "coupling 1 ac"; s "coupling 2 ac"; sleep 3; s "zero 1"; s "zero 2"
  s "coupling 1 dc"; s "coupling 2 dc"; sleep 1
done
s "quit"; exec 3>&-; wait $PID; rm -f $F
