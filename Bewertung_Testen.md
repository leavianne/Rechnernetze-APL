Usage: ./client -a <server> -p <port> -f <file> -w <window>
       -a <server> : Server-Adresse (Default: loopback)
       -p <port>   : Server-Port (Default: 3333)
       -f <file>   : Eingabedatei
       -w <window> : Fenstergröße (1..10)

Usage: ./server -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>]
   -p <port>    : Server-Port (Default: 3333)
   -f <outfile> : Ausgabedatei
   -r <lossReq> : Request-Verlustwahrscheinlichkeit (0.0..1.0)
   -a <lossAck> : ACK-Verlustwahrscheinlichkeit (0.0..1.0)


## 4.0 Grundfunktionalitäten
./server -p 3333 -f output.txt
./client -a ::1 -p 3333 -f input.txt -w 1

	-> funktioniert

## 3.7 Sequenznummern und Intervalle
./server -p 3338 -f out_seq.txt 2>&1 | grep -E "(seq=|expected=)"
./client -a ::1 -p 3338 -f input.txt -w 1 2>&1 | grep -E "(seq=|packet [0-9]+)"

Zeit messen -> ein Paket pro Intervall
time ./client -a ::1 -p 3339 -f input.txt -w 1

Ergebnis:
real	0m0,011s
user	0m0,000s
sys	0m0,004s

Intervalle mit Verlust:
./server -p 3340 -f out_int.txt -a 0.5
./client -a ::1 -p 3340 -f input.txt -w 1

	-> funktioniert nur mit Fenstergröße 3

## 3.3 Sequenzprüfung und ACK
Korrekte Sequenz → ACK OK: funktioniert
./server -p 3341 -f out_ack.txt &
./client -a ::1 -p 3341 -f input.txt -w 1 2>&1 | grep -E "(Received answer|ACK)"

Out-of-Order → ACK Warn: funktioniert
./server -p 3342 -f out_ooo.txt -r 0.3 &
for i in {1..20}; do echo "Zeile $i" >> test_ooo.txt; done
./client -a ::1 -p 3342 -f test_ooo.txt -w 3

Kumulative ACKs
./server -p 3343 -f out_cum.txt &
./client -a ::1 -p 3343 -f input.txt -w 3 2>&1 | grep -E "(Advancing window|freeing)"


