#!/bin/bash

## create the graph
gnuplot -e "set terminal png; set output 'out.png'; set yrange [0:20]; set xlabel 'samples'; set ylabel 'amps'; plot 'plots.dat' with lines notitle"

## there should now be an out.png with our graph. b64 it
base64 out.png > out.b64

## glue it all together
cat measurement.txt > email
cat out.b64 >> email
echo >>email
echo "--xxxx38th parallel--">>email
echo >>email

## line enings must be \r\n for smtp
unix2dos email 2>/dev/null

sendmail -t < email
