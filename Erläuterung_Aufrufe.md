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
make input.txt
```
oder
```
cat > input.txt << 'EOF'
Hallo Amelie
Hallo Anton
Hallo Iryna
Hallo Lea
EOF
```

## Konnektivität und grundlegende Funktionalität

Terminal 1 (Server - ohne Paketverlust)
```
./server -p 3333 -f output_4.0.txt
```
Terminal 2 (Client - Standardfenstergröße)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```
### Überprüfung:
1. output_4.0.txt sollte identisch mit input.txt sein
```
diff input.txt output_4.0.txt
```
2. Beide Terminals sollten "completed"/"Transfer complete" zeigen

## Sequenznummern & Intervalle

Terminal 1 (Server)
```
./server -p 3333 -f output_3.7.txt
```
Terminal 2 (Client - Window=1, Ausgabe zeigt nur 1 Paket/Intervall)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```
### Überprüfung im Output:   
Jedes Paket wird einzeln gesendet und quittiert  

## Sequenznummern & Quittierung

Terminal 1 (Server - mit Paketverlust für Out-of-Order)
```
./server -p 3333 -f output_3.3.txt -r 0.0 -a 0.0
```
Terminal 2 (Client)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```

## Timeout und Retransmission (w=1)

Terminal 1 (Server - 30% ACK-Verlust simulieren)
```
./server -p 3333 -f output_3.0.txt -r 0.0 -a 0.3
```
Terminal 2 (Client - Window=1, Stop-and-Wait)
```
./client -a ::1 -p 3333 -f input.txt -w 1
```

### Überprüfung im Client-Output:    
Die Datei sollte trotz ACK-Verlusts korrekt angekommen sein  

## Go-Back-N mit w > 1

Terminal 1 (Server - kein Verlust)
```
./server -p 3333 -f output_2.7.txt -r 0.0 -a 0.0
```
Terminal 2 (Client - Window=3, mehrere Pakete gleichzeitig)
```
./client -a ::1 -p 3333 -f input.txt -w 3
```

## Fenster-Verwaltung & kumulative ACKs

Terminal 1 (Server)
```
./server -p 3333 -f output_2.3.txt -r 0.0 -a 0.0
```
Terminal 2 (Client - Window=5)
```
./client -a ::1 -p 3333 -f input.txt -w 5
``` 
### Fenster-Verwaltung:  
* ACK for seq 1, freeing 1 packets (base: 0 -> 1)  
* ACK for seq 2, freeing 1 packets (base: 1 -> 2)  
* Etc.  

## Timeout mit Go-Back-N

Terminal 1 (Server - 50% ACK-Verlust)
```
./server -p 3333 -f output_2.0.txt -r 0.0 -a 0.5
```
Terminal 2 (Client - Window=3, Go-Back-N mit Retransmit)
```
./client -a ::1 -p 3333 -f input.txt -w 3
```

## Großer Paketverlust und großes Fenster

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

## Eine größere Datei
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