## VORBEREITUNG

### Terminal 1 & 2: In das Projektverzeichnis wechseln
```
cd /path/to/arq-projekt
```
### Kompilieren
```
make
```
oder
```
gcc -o client client.c clientSy.c error.c
gcc -o server server.c serverSy.c error.c
```

### Test-Eingabedatei erstellen (falls noch nicht vorhanden)
```
cat > input.txt << 'EOF'
Hallo Amelie
Hallo Anton
Hallo Iryna
Hallo Lea
EOF
```

## BEWERTUNG 4,0 - KONNEKTIVITÄT & GRUNDLEGENDE FUNKTIONALITÄT

Ziel: Datei wird fehlerfrei übertragen, Sequenznummern stimmen

Terminal 1 (Server - ohne Paketverlust)
```
./server -p 3333 -f output_4.0.txt
```
Terminal 2 (Client - Standardfenstergröße)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```
Überprüfung:
1. output_4.0.txt sollte identisch mit input.txt sein
```
diff input.txt output_4.0.txt
```
2. Beide Terminals sollten "completed"/"Transfer complete" zeigen

## BEWERTUNG 3,7 - SEQUENZNUMMERN & INTERVALLE
Ziel: Sequenznummern korrekt, max 1 Paket pro Intervall gesendet  
Non-blocking Socket asynchron, Intervalle eingehalten  

Terminal 1 (Server)
```
./server -p 3333 -f output_3.7.txt
```
Terminal 2 (Client - Window=1, Ausgabe zeigt nur 1 Paket/Intervall)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```
### Überprüfung im Output:  
[CLIENT] SEND: type=D seq=0 (Packet 0)  
[CLIENT] SEND: type=D seq=1 (Packet 1 - nach ACK von Packet 0)  
Etc.  
Jedes Paket wird einzeln gesendet und quittiert  

## BEWERTUNG 3,3 - SEQUENZNUMMERN-VERIFIKATION & QUITTIERUNG
Ziel: Server prüft Sequenznummern, sendet kumulative ACKs  
Out-of-Order Pakete werden gedroppt  

Terminal 1 (Server - mit Paketverlust für Out-of-Order)
```
./server -p 3333 -f output_3.3.txt -r 0.0 -a 0.0
```
Terminal 2 (Client)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```
### Überprüfung im Server-Output:  
[SERVER] RECV: type=D seq=0  
[SERVER] DATA seq=0 INORDER  
[SERVER] ACK: next expected = 1  
[SERVER] SEND: type=O seq/err=1  

## BEWERTUNG 3,0 - TIMEOUT & RETRANSMISSION (W=1)
Ziel: Bei ACK-Verlust erfolgt Retransmission nach Timeout  
Stop-and-Wait funktioniert mit Paketverlust

Terminal 1 (Server - 30% ACK-Verlust simulieren)
```
./server -p 3333 -f output_3.0.txt -r 0.0 -a 0.3
```
Terminal 2 (Client - Window=1, Stop-and-Wait)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```

### Überprüfung im Client-Output:  
[CLIENT] SEND: type=D seq=0  
[CLIENT] Preparing DATA packet 0  
[CLIENT] ACK timeout (wenn ACK verloren geht)  
[CLIENT] TIMEOUT: Retransmitting window (base=0, count=1)  
[CLIENT] SEND: type=D seq=0 (Retransmission!)  
Die Datei sollte trotzdem korrekt angekommen sein  

## BEWERTUNG 2,7 - GO-BACK-N MIT W > 1
Ziel: Fensterbasiertes Senden mit W=3 oder höher  
Ringpuffer funktioniert, mehrere Pakete parallel  

Terminal 1 (Server - kein Verlust)
```
./server -p 3333 -f output_2.7.txt -r 0.0 -a 0.0
```
Terminal 2 (Client - Window=3, mehrere Pakete gleichzeitig)
```
./client -a ::1 -p 3333 -f input.txt -w 3
```
### Überprüfung im Client-Output:  
[CLIENT] Preparing DATA packet 0  
[CLIENT] Stored packet 0 in buffer[0] (window=1/3)  
[CLIENT] SEND: type=D seq=0  
[CLIENT] Preparing DATA packet 1  
[CLIENT] Stored packet 1 in buffer[1] (window=2/3)  
[CLIENT] SEND: type=D seq=1  
[CLIENT] Preparing DATA packet 2  
[CLIENT] Stored packet 2 in buffer[2] (window=3/3)  
[CLIENT] SEND: type=D seq=2  
[CLIENT] Window FULL (3/3), waiting for ACK...  
[CLIENT] ACK for seq 3, freeing 3 packets  

## BEWERTUNG 2,3 - FENSTER-VERWALTUNG & KUMULATIVE ACKs
Ziel: Fenster verschiebt sich korrekt bei kumulativen ACKs  
Hello-Handshake funktioniert

Terminal 1 (Server)
```
./server -p 3333 -f output_2.3.txt -r 0.0 -a 0.0
```
Terminal 2 (Client - Window=5)
```
./client -a ::1 -p 3333 -f input.txt -w 5
```
### Überprüfung im Client-Output:  
[CLIENT] Sending HELLO (window=5)  
[CLIENT] HELLO acknowledged  
### Fenster-Verwaltung:  
* ACK for seq 1, freeing 1 packets (base: 0 -> 1)  
* ACK for seq 2, freeing 1 packets (base: 1 -> 2)  
* Etc.  

## BEWERTUNG 2,0 - TIMEOUT MIT GO-BACK-N
Ziel: Bei Timeout werden ALLE unbestätigten Pakete erneut gesendet  
Go-Back-N Funktionsweise mit W>1 und Paketverlust  

Terminal 1 (Server - 50% ACK-Verlust)
```
./server -p 3333 -f output_2.0.txt -r 0.0 -a 0.5
```
Terminal 2 (Client - Window=3, Go-Back-N mit Retransmit)
```
./client -a ::1 -p 3333 -f input.txt -w 3
```
### Überprüfung im Client-Output:  
Mehrere Pakete werden gesendet  
[CLIENT] TIMEOUT: Retransmitting window (base=0, count=3)  
[CLIENT] SEND: type=D seq=0 (Retransmission!)  
[CLIENT] SEND: type=D seq=1 (Retransmission!)  
[CLIENT] SEND: type=D seq=2 (Retransmission!)  
### ODER bei späteren Timeouts:  
[CLIENT] TIMEOUT: Retransmitting window (base=1, count=2)  
[CLIENT] SEND: type=D seq=1  
[CLIENT] SEND: type=D seq=2  

# BONUS-TESTS
## Test mit großem Paketverlust und großem Fenster

Terminal 1
```
./server -p 3333 -f output_bonus.txt -r 0.5 -a 0.2
```
Terminal 2 (Window=10, größtes Fenster)
```
./client -a ::1 -p 3333 -f input.txt -w 10
```
### Überprüfung: 
Trotz 50% Paketverlust funktioniert der Transfer

## Test mit größerer Datei
Terminal 1: Größere Testdatei erstellen
```
cat > input_large.txt << 'EOF'
Zeile 1
Zeile 2
Zeile 3
Zeile 4
Zeile 5
Zeile 6
Zeile 7
Zeile 8
Zeile 9
Zeile 10
EOF
```
Terminal 1 (Server)
```
./server -p 3333 -f output_large.txt
```
Terminal 2 (Client)
```
./client -a ::1 -p 3333 -f input_large.txt -w 5
```