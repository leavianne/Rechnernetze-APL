# Designentscheidungen
## Architekturentscheidungen
  - drei Schichten wie in ARQ.rar vorgegeben, damit die Funktionatitäten sauber getrennt sind, wie es in der Aufgabenstellung gefordert ist 
    - Anwendungsschicht: client.c und server.c
    - ARQ-Schicht: clientSy.c und serverSy.c
    - UDP-Schicht: auch in clientSy.c und serverSy.c integriert
  -  einfach zu Testen der einzelnen Komponenten
  - keine Threads (wie in der Aufgabenstellung gefordert) und nur ein Socket pro Instanz
  - Verbindung zwischen ARQ-Schicht und Anwendungs von Außen mit Anwendungs-Callbacks

## Protokoll-Design
  - 32 Bit Design für Sequenznummern, verhindert Überlaufprobleme bei der Input-Textdatei
  - anpassbare Fenstergröße (1 bis 10) mit Ringpuffer (effizientes Fensterverschieben)
  - Speicher wird wird wärend des Kompilierens reserviert und nicht dynamisch zur Laufzeit 
    - verhindet Heap-Fragmentierung 
  - doppelte Puffergröße: verhindert Indexkollisionen (wenn zwei Eingabedateien auf den gleichen Speicher zugreifen wollen
  - Zeitsteuerung in 100 ms Intervallen für Synchonität
  - Verwendung von ACK -> bestätigtalle Packete < N
  - Drei-Phasen-Protokoll
    - für zuverlässigen Verbindungsaufbau und Abbau mit klarer Phasentrennung
    - Verbindungsaufbau und Abbau wird in der Konsole angezeigt

## Datenstrukturen
  - Request-Paket (Client -> Server)
    - mit Requesttype, Datenlänge, Sequenznummer und den Nutzdaten
  - Antwortpaket (Server -> Client)
    - mit Answertype, dem reservierten Speicher und der nächsten Sequenznummer die erwartet wird
  - für jede Textzeile 512 Byte Puffer reserviert
  - Trennung zwischen Steuerinformationen und Nutzdaten

### Ringpuffer
  - Arraybasierter Ringpuffer mit Modulo
  - Vorhersehbare Speichernutzung ohne dynamischen Speicher

### Zustandverwaltung
  - globale statische Variablen für dei verschiedenen Protokollzustände
  - direkter Zugriff auf Zustände und Zustandsverwaltung

## Algorithmen
### Go-Back-N Algorithmus 
  - wie in der Aufgabenstellung gefordert
  - unbestätigte Packete werden erneut verschickt
  - garantiert die korrekte Reihenfolge beim Empfänger

### Zeitgesteuerter Algorithmus
  - Intervall-Basierter Timer mit select() -> siehe Zeitsynchornes System
    - Implementiertbar ohne Threads
    - Timeout- und Event-Handling möglich damit auf die zeitbasierten Abläufe reagiert werden kann

### Fensterverwaltung
  - effiziente Fensterverwaltung
  - Sendefenstergröße ist über N bestimmt und Empfängerfensterliste ist fest auf 1 gesetzt
  - Sender kann N Packete ohne Bestätigung versenden
  - Fenster wird freigegeben wenn Sender eine Bestätigung erhält

## Netzwerk-Implementierung 
  - IPv6 wie in Aufgabenstellung gefordert
  - einfache Adressverwaltung mit IPv6

### Socket-Konfiguration
  - Client: Non-blocking Socket mit select()
    - ermöglicht zeitsynchone Steuerung 
  - Server: Blocking Socket 
  - ein Socket pro Instanz: ein Socket für alle Operationen
    - einfache Socket-Verwaltung und wenig Port-Konflikte
    - einfache Fehlerbehandlung

## Fehlerbehandlung 
  - Zufallsbasierte Paket und ACK-Verluste durch die Variable lossReq bei Requestverlusst und Variable lossAck bei ACK verlusst
  - Go-Back-N Retransmission bei Timeout: nach Timeout wird das Packet erneut gesendet
    - das garantiert die zuverlässige Übertragung der Packete
  - Protokoll-Fehlerbehandlung funktioniert bereits

## Performance-Aspekte
  - statischer Puffer damit nicht ständig neuer Speicher freigegeben werden muss
  - maximale Fenstergröße bei 10



















   
