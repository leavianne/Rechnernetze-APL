## Rechnernetze und Kommunikationssysteme APL - WS 25/26

Zuverlässiges Übertragungsprotokoll auf Basis von **UDP über IPv6**. Ein Client sendet eine Textdatei **zeilenweise** an einen Server. Da UDP selbst keine Zuverlässigkeit bietet, wird sie durch ein eigenes **ARQ-Protokoll (Go-Back-N)** mit **positiven kumulativen Acknowledgements** hergestellt.

## Funktionsweise

**Verbindung**
- Der Client baut die Verbindung mit einem `HELLO`-Paket auf. Erst nach der Quittung beginnt die Datenübertragung.
- Nach der Übertragung beendet der Client die Verbindung mit einem `CLOSE`-Paket, das ebenfalls quittiert wird.

**Client (Sender)**
- Sendefenster als **Ringpuffer**, Fenstergröße 1 bis 10 Pakete.
- **Zeitsynchron:** Pro Zeitschlitz (z. B. 100 ms) wird höchstens ein Paket gesendet. Eine Neuübertragung nach Timeout hat Vorrang.
- Warten auf ACK oder Intervallende mit `select()`. Auch bei früh eintreffendem ACK wird bis zum Intervallende gewartet.
- Bei Timeout werden das betroffene und alle folgenden unquittierten Pakete erneut gesendet (Go-Back-N).
- ACKs außerhalb des Sendefensters werden verworfen.

**Server (Empfänger)**
- Prüft die Sequenznummern und reicht nur in Reihenfolge empfangene Pakete an die Anwendung weiter (Schreiben in die Datei).
- Bestätigt mit der als nächstes erwarteten Sequenznummer. Pakete außerhalb der Reihenfolge werden verworfen, es gibt keinen Empfangspuffer.

Es werden keine Threads verwendet, pro Instanz gibt es genau einen Socket. Paket- und ACK-Verluste lassen sich über Programmargumente simulieren, alle Abläufe werden auf der Konsole ausgegeben.

## Repository-Struktur

| Pfad | Inhalt |
|---|---|
| `src/` | Quellcode (Client, Server, ARQ-Logik) |
| `img/` | Abbildungen (Diagramme) |
| `Erläuterung_Aufrufe.md` | Aufruf von Client und Server |
| `Desigentscheidungen.md` | Dokumentation der Designentscheidungen |
| `Selbstständigkeitserklärung und Quellen.md` | Selbstständigkeitserklärung und Quellen |
| `Selbstständigkeits- und KI-Erklärung.md` | Selbstständigkeits- und KI-Erklärung |

## Kompilieren und Aufruf

Ausgelegt für Linux (Labor S311). Die genaue Syntax von Client und Server steht in [Erläuterung_Aufrufe.md](Erläuterung_Aufrufe.md).

## Verfasst von

- Anton Meyer
- Iryna Skworzow
- Amelie Johanna Trautmann
- Lea Vianne Eichler
