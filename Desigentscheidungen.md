# Designentscheidungen für ARQ Go-Back-N Implementierung

## Entscheidungen beim Aufbau
- **Anwendungsschicht**: client.c und server.c, wie in der Aufgabenstellung gefordert:
  -File Input/Output und User-Interface, aufrufen der Funktionen der ARQ-Schicht

- **ARQ-Schicht**: clientSy.c und serverSy.c,  wie in der Aufgabenstellung gefordert:
  - Go-Back-N Algorithmus Logik, Fenster, Timer, Retransmissionen,
  - UDP: Socket-Verwaltung

-Wie Gefordert: Ein Socket pro Client/Server-Instanz, Keine Threads.

## Go-Back-N Algorithmus
**Sender (Client):**
- Sendefenster: [base, base+window_size-1]
- Pro Intervall max. 1 neues Paket in Fenster
- Bei ACK: Fenster vorwärts schieben
- Bei Timeout: Alle Pakete im Fenster erneut senden

- `-w 1`: Stop-and-Wait (einfach)
- `-w 3`: Typisch balanciert
- `-w 10`: Maximum (schnellste Übertragung)

**Empfänger (Server):**
- Akzeptiert nur in-order Pakete (SeNr == nextExpected)
- Sendet kumulative ACK mit nextExpected
- Duplikate werden erkannt und ignoriert
- Out-of-order Pakete werden verworfen

### Fenster
- Anpassbare Fenstergröße: 1-10 Pakete (Parameter `-w`)
- Ringpuffer
- Verhindert Indexkollisionen bei Fensterverschiebung

### Zeit
- Intervall-basierter Timer: 100 ms pro Intervall (schnelle Reaktion auf Timeouts, aber erlaubt auch ein wenig Netzwerk-Latenz)
- Timeout nach 3 Intervallen (300 ms)
- Client wartet in select() auf ACK oder Timeout

### Drei Phasen
1. **HELLO**: Verbindungsaufbau, Datei öffnen
2. **DATA**: Zuverlässige Datenübertragung mit Fenster
3. **CLOSE**: Verbindungsabbau, Datei schließen

## Verwaltung
### Speicherverwaltung
- Ringpuffer und Timer-Arrays werden beim Kompilieren allokiert, nicht dynamisch
- Einfacher, damit beim Coding umzugehen und vorhersehbarer (Wenn auch nicht so speicherfreundlich)

### Globale Variablen
Globale statische Variablen für Protokollzustand: (base, next_seqnum, packet_count (Fenster),connection_active, current_packet_num (Transfer-State)...)
  -Für diesen Code einfacher zu handhaben als viele Pointer und Übergaben in Funktionen, 
  auch wenn es keine "good practice" ist

## Netzwerk
- Non-blocking Client-Socket für Event-based Handling
- Blocking Server-Socket mit select()

## Fehlerbehandlung

### Paketverlust-Simulation
Parameter `-r` und `-a` simulieren Fehlerquoten:
- `-r 0.2`: 20% der Requests verworfen
- `-a 0.1`: 10% der ACKs verworfen
- Tests das Go-Back-N Retransmission-Verhalten

### Timeout & Retransmission (Go-Back-N)
- Jedes Paket hat eigenen Timer
- Bei Timeout: Alle unbestätigten Pakete neu senden

### Protokoll-Fehler
- Falsche Sequenznummern werden erkannt
- File Input/Output Fehler werden gemeldet