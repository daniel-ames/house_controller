#!/bin/bash

## create the graph
gnuplot -e "set terminal png; set output 'out.png'; set yrange [0:20]; set xlabel 'seconds'; set ylabel 'amps'; set title ''; plot 'plots.dat' with lines"

## there should now be an out.png with our graph. b64 it
base64 out.png > out.b64

## glue it all together
cat measurement.txt > email
cat out.b64 >> email
echo >>email
echo "--xxxx38th parallel--">>email
echo >>email

## line enings must be \r\n for smtp
unix2dos email

sendmail -t < email
