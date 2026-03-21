#!/bin/bash
set -e

cd "$1"

case "$2" in
  sewage)
    gnuplot -e "set terminal png; set output 'out.png'; set yrange [0:20]; set xlabel 'samples'; set ylabel 'amps'; plot 'plots.dat' with lines notitle"
    ;;
  wellhouse)
    gnuplot -e "set terminal png; set output 'out.png'; set yrange [0:20]; set xlabel 'samples'; set ylabel 'amps'; plot 'plots.dat' using 1:2 with lines title 'L1', 'plots.dat' using 1:3 with lines title 'L2'"
    ;;
  *)
    echo "usage: $0 <temp_dir> <sewage|wellhouse>" >&2
    exit 1
    ;;
esac

base64 out.png > out.b64
cat measurement.txt > email
cat out.b64 >> email
echo >> email
echo "--xxxx38th parallel--" >> email
echo >> email
unix2dos email 2>/dev/null || true
sendmail -t < email
