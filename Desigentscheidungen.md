# Designentscheidungen für ARQ Go-Back-N Implementierung

## 1. Architekturentscheidungen

### 1.1 Drei-Schichten-Modell
- **Anwendungsschicht**: client.c und server.c
  - Datei I/O und User-Interface
  - Callbacks für ARQ-Schicht (appStartFn, appWriteFn, appEndFn)

- **ARQ-Schicht**: clientSy.c und serverSy.c
  - Go-Back-N Algorithmus, Fenster, Timer, Retransmissionen
  - Protokolllogik für Sender und Empfänger

- **UDP/SAP-Schicht**: in clientSy.c und serverSy.c integriert
  - Socket-Verwaltung und sendto/recvfrom Operationen

### 1.2 Callback-Architektur
Die ARQ-Schicht ruft Anwendungs-Callbacks auf statt direkt auf Dateien zuzugreifen. Das entkoppelt Protokoll und Dateilogik.

### 1.3 Single-Socket-Design
Ein Socket pro Client/Server-Instanz. Vereinfachte Verwaltung, keine Threads, asynchrones Event-Handling mit select().

## 2. Protokoll-Design

### 2.1 Paketstruktur
- **Request** (Client → Server): Typ, Sequenznummer, Datenlänge, Nutzdaten
- **Answer** (Server → Client): Typ, kumulative ACK (nächst erwartete Seq.Nr)

### 2.2 Go-Back-N Algorithmus
**Sender (Client):**
- Sendefenster: [base, base+window_size-1]
- Pro Intervall max. 1 neues Paket in Fenster
- Bei ACK: Fenster vorwärts schieben
- Bei Timeout: Alle Pakete im Fenster erneut senden

**Empfänger (Server):**
- Akzeptiert nur in-order Pakete (SeNr == nextExpected)
- Sendet kumulative ACK mit nextExpected
- Duplikate werden erkannt und ignoriert
- Out-of-order Pakete werden verworfen

### 2.3 Fensterverwaltung
- Anpassbare Fenstergröße: 1-10 Pakete (Parameter `-w`)
- Ringpuffer mit doppelter Größe (20 statt 10)
- Verhindert Indexkollisionen bei Fensterverschiebung

### 2.4 Zeitsteuerung
- Intervall-basierter Timer: 100 ms pro Intervall
- Timeout nach 3 Intervallen (300 ms)
- Client wartet in select() auf ACK oder Timeout
- Keine Threads nötig

### 2.5 Drei-Phasen-Protokoll
1. **HELLO**: Verbindungsaufbau, Datei öffnen
2. **DATA**: Zuverlässige Datenübertragung mit Fenster
3. **CLOSE**: Verbindungsabbau, Datei schließen

## 3. Datenstrukturen

### 3.1 Statische Speicherverwaltung
- Ringpuffer und Timer-Arrays werden beim Kompilieren allokiert
- Keine dynamische Speichernutzung zur Laufzeit
- Verhindert Heap-Fragmentierung
- Vorhersehbare und effiziente Speichernutzung (~10 KB Overhead)

### 3.2 Zustandsverwaltung
Globale statische Variablen für Protokollzustand:
- base, next_seqnum, packet_count (Fenster)
- connection_active, current_packet_num (Transfer-State)

## 4. Netzwerk-Implementierung

### 4.1 IPv6 mit UDP
- Dual-Stack fähig (IPv6 mit IPv4-mapped Addresses)
- Einfache Socket-Konfiguration
- Non-blocking Client-Socket für Event-based Handling
- Blocking Server-Socket mit select()

## 5. Fehlerbehandlung

### 5.1 Paketverlust-Simulation
Parameter `-r` und `-a` simulieren Fehlerquoten:
- `-r 0.2`: 20% der Requests verworfen
- `-a 0.1`: 10% der ACKs verworfen
- Tests das Go-Back-N Retransmission-Verhalten

### 5.2 Timeout & Retransmission
- Jedes Paket hat eigenen Timer
- Bei Timeout: Alle unbestätigten Pakete neu senden
- Client wartet max. 50 Intervalle (5 Sekunden) pro Paket
- Garantiert zuverlässige Übertragung

### 5.3 Protokoll-Fehler
- DATA ohne aktiven Transfer wird abgelehnt
- Falsche Sequenznummern werden erkannt
- Datei I/O Fehler werden gemeldet
- Unbekannte Request-Typen werden ignoriert

## 6. Performance-Aspekte

### 6.1 Speicher-Effizienz
- Client-Overhead: ~10 KB für Protokoll-Puffer
- Server: Streaming ohne Puffering
- 512 Byte pro Zeile reserviert

### 6.2 Fenster-Größe und Durchsatz
- Größeres Fenster = höherer Durchsatz (aber mehr Speicher)
- `-w 1`: Stop-and-Wait (einfach)
- `-w 3`: Typisch balanciert
- `-w 10`: Maximum (schnellste Übertragung)

### 6.3 Timing-Parameter
100 ms Intervalle ermöglichen schnelle Reaktion auf Timeouts, ohne zu aggressive zu sein bei Netzwerk-Latenzen.

## 7. Zusammenfassung

| Aspekt | Entscheidung |
|--------|-------------|
| Architektur | 3 Schichten mit Callbacks |
| Protokoll | Go-Back-N mit kumulativen ACKs |
| Fenster | Dynamisch 1-10, Ringpuffer |
| Timer | Interval-basiert 100 ms, keine Threads |
| Speicher | Statisch, kein malloc() zur Laufzeit |
| Netzwerk | IPv6 UDP, Non-blocking Client |
| Fehlertoleranz | Timeout + Retransmission |