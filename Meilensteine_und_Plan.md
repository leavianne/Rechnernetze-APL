Meilensteine für die APL-Implementierung
Meilenstein 1: Grundlegende UDP-Konnektivität (4.0 Punkte)

    UDPv6-Sockets auf Client und Server einrichten

    IPv6-Kommunikation zwischen Client und Server auf gleichem Rechner

    Basis-File-Transfer (unidirektional Client → Server)

    Ausgabe der empfangenen Daten in serverseitiger Datei

    Einfache Kommandozeilenargument-Verarbeitung

Meilenstein 2: Sequenznummern und grundlegende ARQ-Logik (3.7 Punkte)

    Sequenznummern in Paketen (Client-Seite) implementieren

    Sequenznummer-Prüfung serverseitig implementieren

    Intervall-basiertes Senden (max. 1 Paket/Intervall)

    Non-blocking Socket auf Client-Seite mit select()

    Paket-Quittierungen (ACKs) serverseitig senden

Meilenstein 3: Stop-and-Wait (W=1) mit Timeout (3.3 Punkte)

    Quittungsprüfung serverseitig für korrekte SeqNr

    ACK-Generierung für korrekt empfangene Pakete

    Fehlerbehandlung für out-of-order Pakete

    Stop-and-Wait Logik für Fenstergröße W=1

Meilenstein 4: Timeout und Retransmission für W=1 (3.0 Punkte)

    Timer-Implementierung für Pakete

    Timeout-Erkennung bei fehlenden ACKs

    Retransmission von nicht quittierten Paketen

    Fehlerfall-Tests (ausbleibende/verzögerte ACKs)

    Protokollrobustheit für W=1 nachweisen

Meilenstein 5: Go-Back-N für W>1 (2.7 Punkte)

    Fensterverwaltung für W>1 implementieren

    Ringpuffer für Sendefenster auf Client-Seite

    Fenster-basiertes Senden (mehrere Pakete im Fenster)

    Pufferung bereits gesendeter, unquittierter Pakete

    Fenstergröße als Parameter (1-10)

Meilenstein 6: Kumulative ACKs und Fensterverschiebung (2.3 Punkte)

    Kumulative ACK-Verarbeitung auf Client-Seite

    Fenster-Verschiebung bei ACK-Empfang

    ACK-Prüfung auf Gültigkeit (innerhalb Fenster)

    Verbindungsaufbau mit Hello-Paket/Hello-ACK

    Fehlerbehandlung für ungültige ACKs

Meilenstein 7: Go-Back-N Retransmission (2.0 Punkte)

    Timeout-basierte Retransmission aller unbestätigter Pakete

    Ringpuffer-Implementierung komplettieren

    Fenster-Reset bei Timeout

    Protokoll-Konsistenz bei Retransmissions

Meilenstein 8: Zeitsynchronisation und entfernte Kommunikation (1.7 Punkte)

    Strikte Intervall-Einhaltung (100ms)

    select()-basierte Zeitsteuerung

    Ruhephase bis Intervallende bei frühem ACK

    Übertragung zwischen entfernten Rechnern testen

    Performance-Optimierung

Meilenstein 9: Verbindungslebenszyklus (1.3 Punkte)

    Verbindungsaufbau (Hello) mit Bestätigung

    Verbindungsabbau (Close) mit Bestätigung

    Fehlerbehandlung bei Close

    Zustandsverwaltung für Verbindung

    Ressourcenfreigabe bei Abbruch

Meilenstein 10: Sonderfälle und Robustheit (1.0 Punkte)

    Close-Nachrichten in Fehlerfällen

    Paketverlust-Simulation (Parameter -r, -a)

    Fehlererholung nach Timeouts

    Ausführliche Logging für Nachvollziehbarkeit

    Komplette Testfälle abdecken

Technische Implementierungsstrategie
Phase 1: Prototyp (Meilensteine 1-3)

    Einfacher UDP-Transfer ohne ARQ

    SeqNr-Einführung in Paketstruktur

    Grundlegendes ACK-Protokoll

Phase 2: Stop-and-Wait (Meilensteine 4)

    Timer-Integration

    Retransmission-Logik

    Fehlerfall-Tests

Phase 3: Go-Back-N (Meilensteine 5-7)

    Ringpuffer-Design

    Fensterverwaltung

    Kumulative ACK-Verarbeitung

Phase 4: Verfeinerung (Meilensteine 8-10)

    Zeitsynchronisation

    Verbindungsmanagement

    Robustheit und Fehlerbehandlung

Test- und Validierungsplan
Funktionale Tests

    Fehlerfreier Transfer (kleine/größe Dateien)

    Paketverlust-Simulation (Request/ACK-Verlust)

    Fenstergrößen-Variation (1, 3, 5, 10)

    Timeout-Szenarien (verschiedene Timeout-Werte)

Nicht-funktionale Tests

    Performance bei verschiedenen Fenstergrößen

    Robustheit bei hoher Paketverlustrate

    Ressourcennutzung (Speicher, CPU)

Protokollkonformität

    Weg-Zeit-Diagramme erstellen

    Zustandsdiagramme validieren

    Protokoll-Logs analysieren

Bewertungskriterien-Checklist
Für 4.0 Punkte (Basis):

    UDPv6-Verbindung funktioniert

    File-Transfer (Client→Server) möglich

    IPv6 auf lokalem Rechner getestet

Für 3.0 Punkte (ARQ-Grundlagen):

    Sequenznummern implementiert

    ACK-Protokoll funktioniert

    Stop-and-Wait (W=1) getestet

Für 2.0 Punkte (Go-Back-N):

    Fensterverwaltung für W>1

    Ringpuffer korrekt implementiert

    Retransmission bei Timeout

Für 1.0 Punkte (Vollständigkeit):

    Verbindungsaufbau/-abbau

    Fehlerbehandlung komplett

    Alle Testfälle bestanden
