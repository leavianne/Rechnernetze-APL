## Zusätzliche Hinweise zur Verwendung der Diagramme

Für Tests mit Packet/ACK loss setze beim Server beim Start die Parameter -r (request loss) und/oder -a (ack loss), z. B.:
```
./server -p 3333 -f output.txt -r 0.3 -a 0.2
```
Auf der Clientseite die Fenstergröße wählen mit -w (1..10).  
Beobachte in Logs die Zustandsübergänge (HELLO, ENQUEUE, SEND, ACK RECEIVED, TIMEOUT, RETRANSMIT, CLOSE).  
