## 3.0 - Timeout für W=1

### Test 1: Timeout bei ausbleibendem ACK

Server mit 100% ACK-Verlust
./server -p 3345 -f out_timeout.txt -a 1.0 &
sleep 1

Client starten (sollte abbrechen)
./client -a ::1 -p 3345 -f input.txt -w 1 2>&1 | grep -E "(Timeout|Retransmission needed|No response after)"

Im Client-Log prüfen:

    "Timeout for packet 0"

    "Retransmission needed"

    "No response after 3 retries"

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 2: Verzögerte Quittung

Mit tcpdump beobachten, dass nach ~300ms Retransmission kommt
sudo tcpdump -i lo -n "port 3346" -c 10 &
TCPDUMP_PID=$!

./server -p 3346 -f out_delay.txt &
sleep 1

./client -a ::1 -p 3346 -f input.txt -w 1

kill $TCPDUMP_PID

Tcpdump sollte zeigen: Paket, 300ms Pause, gleiches Paket erneut

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 3: Erfolgreiche Übertragung nach Timeout
Server mit 50% ACK-Verlust

./server -p 3347 -f out_recover.txt -a 0.5 &
sleep 1

./client -a ::1 -p 3347 -f input.txt -w 1 2>&1 | tee client_recover.log
Analyse:

echo "=== Timeouts ==="
grep -c "Timeout" client_recover.log
echo "=== Retransmissions ==="
grep -c "Retransmitted" client_recover.log
echo "=== Erfolg ==="
grep -c "Transfer completed successfully" client_recover.log
Sollte zeigen:
- Einige Timeouts
- Retransmissions
- Aber letztendlich "Transfer completed successfully"

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
## 2.7 - Go-Back-N für W>1

### Test 1: Fensterbasierendes Senden
Mit window=3 testen

./server -p 3348 -f out_gbn1.txt &
sleep 1
./client -a ::1 -p 3348 -f input.txt -w 3 2>&1 | grep -E "(Stored packet|count=)"
Erwartet:
"Stored packet 0 in buffer[0] (count=1)"
"Stored packet 1 in buffer[1] (count=2)"
"Stored packet 2 in buffer[2] (count=3)"
Danach Warten auf ACK

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 2: Pufferung auf Client-Seite
Größere Datei mit window=5

for i in {1..10}; do echo "Data packet $i" >> big.txt; done

./server -p 3349 -f out_gbn2.txt &
sleep 1
./client -a ::1 -p 3349 -f big.txt -w 5 2>&1 | grep -E "(buffer[|packet_count)"
Zeigt, dass mehrere Pakete gepuffert werden

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 3: Go-Back-N bei Paketverlust
Server mit Paketverlust

./server -p 3350 -f out_gbn3.txt -r 0.4 & # 40% Verlust
sleep 1

./client -a ::1 -p 3350 -f input.txt -w 3 2>&1 | grep -E "(Retransmitting window|Time)"
Sollte zeigen:
"Retransmitting window (base=..., count=...)"
Mehrere Pakete werden neu gesendet

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 4: Maximale Fenstergröße
Window=10 (Maximum)

./server -p 3351 -f out_max.txt &
sleep 1

./client -a ::1 -p 3351 -f input.txt -w 10 2>&1 | grep -E "(Window full|count=10)"
Bei ausreichend Daten: "Window full (10/10)"

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
## 2.3 - Fensterschaltung bei ACK

### Test 1: Kumulative ACK erweitert Fenster

./server -p 3352 -f out_win1.txt &
sleep 1
./client -a ::1 -p 3352 -f input.txt -w 3 2>&1 | grep -E "(Advancing window|freeing)"
Bei ACK mit seq=3:
"Advancing window (0 -> 3), freeing 3 packets"
base wird auf 3 gesetzt

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 2: Hello-ACK bestätigt Verbindung
Hello/ACK-Handshake prüfen

./server -p 3353 -f out_hello.txt &
sleep 1
./client -a ::1 -p 3353 -f input.txt -w 1 2>&1 | grep -E "(Hello|acknowledged)"
Erwartet:
Client: "Sending Hello"
Client: "Received answer (type=H, seq/err=0)"
Client: "Hello acknowledged"
Server: "Received Hello"
Server: "Sent answer (type=H, seq/err=0)"

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
## 2.0 - Timeout + Retransmission aller unbestätigter Pakete

### Test 1: Go-Back-N Retransmission
Mit window=3 und ACK-Verlust

./server -p 3354 -f out_retry.txt -a 0.7 & # 70% ACK-Verlust
sleep 1

./client -a ::1 -p 3354 -f input.txt -w 3 2>&1 | grep -B2 -A2 "Retransmitting window"
Sollte zeigen, dass ALLE Pakete im Fenster neu gesendet werden
"Retransmitting window (base=0, count=3)"

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 2: Ringpuffer-Implementierung
Mit seq > 20 testen (Ringpuffer-Wraparound)

for i in {1..30}; do echo "Line $i" >> wrap.txt; done

./server -p 3355 -f out_wrap.txt &
sleep 1
./client -a ::1 -p 3355 -f wrap.txt -w 5
Sollte keine Speicherfehler geben

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 3: Mehrere Retransmission Cycles
Extrem hoher Verlust

./server -p 3356 -f out_multi.txt -a 0.9 -r 0.9 &
sleep 1

./client -a ::1 -p 3356 -f input.txt -w 2 2>&1 | grep -c "Retransmitting window"
Im Log: Mehrere "Retransmitting window" Einträge
Max 3 Retries pro Paket

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
## 1.7 - Intervalle + Remote

### Test 1: Saubere Intervalle
Zeitliche Abläufe prüfen
Client-Log analysieren: Zeit zwischen Send und Retransmission
Sollte ~300ms sein (3 × GBN_TIMEOUT_INT_MS)

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 2: Übertragung zwischen entfernten Rechnern
Auf anderem Rechner im Netzwerk:
Server: ./server -p 3333 -f remote_out.txt
Auf Client-Rechner:
./client -a <SERVER_IPV6> -p 3333 -f input.txt -w 1
IPv6-Adressen:
- Global: 2001:db8::...
- Link-local: fe80::...

Alternative für Test ohne zweiten Rechner:
Mit verschiedenen Loopback-Adressen

./server -p 3357 -f local1.txt &
sleep 1
./client -a ::1 -p 3357 -f input.txt -w 1
Oder mit Hostname

./server -p 3358 -f local2.txt &
sleep 1
./client -a localhost -p 3358 -f input.txt -w 1

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 3: Performance mit verschiedenen Windows

for w in 1 3 5 10; do
echo "=== Testing window=$w ==="
./server -p 3359 -f perf$w.txt &
sleep 1
time ./client -a ::1 -p 3359 -f medium.txt -w $w 2>&1 > /dev/null
wait
echo ""
done

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
## 1.3 - Verbindungsabbau

### Test 1: Normales Close

./server -p 3360 -f out_close.txt &
sleep 1
./client -a ::1 -p 3360 -f input.txt -w 1 2>&1 | grep -E "(Close|acknowledged)"
Erwartet:
Client: "Sending Close"
Client: "Received answer (type=O, seq/err=...)"
Client: "Close acknowledged"
Server: "Received Close"
Server: "Connection closed by client"

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen

### Test 2: Datei wird geschlossen
Test mit strace:

strace -e trace=open,close ./server -p 3361 -f trace.txt &
SERVER_PID=$!
sleep 1
./client -a ::1 -p 3361 -f input.txt -w 1
sleep 1
kill $SERVER_PID
Sollte close() für die Datei zeigen

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
## 1.0 - Close-Fehlerbehandlung

### Test 1: Timeout beim Close
Server mit 100% ACK-Verlust für Close

./server -p 3362 -f out_close_fail.txt -a 1.0 &
SERVER_PID=$!
sleep 1

./client -a ::1 -p 3362 -f input.txt -w 1 2>&1 | grep -E "(Close|Timeout|No response)"

kill $SERVER_PID
Client sollte Timeouts haben und dann aufräumen

Status: [ ] Nicht getestet [ ] Erfolg [ ] Fehlgeschlagen
