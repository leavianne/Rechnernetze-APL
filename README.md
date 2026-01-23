## Zu erstellende zusätzliche Dateien

    Erklärung der selbstständigen Anfertigung

    Designentscheidungen.md – Dokumentation eurer Design-Entscheidungen

        Wie wird der Ringpuffer implementiert?

        Wie werden Timer realisiert?

        Wie werden Sequenznummern verwaltet?

    Zustandsdiagramme für Sender (Client) und Empfänger (Server)

    Weg-Zeit-Diagramme – für verschiedene Szenarien:

        Fehlerfreier Fall

        Mit Paketverlusten

        Unterschiedliche Fenstergrößen

## <code style="color : red">Alles implementiert?</code> Client-Implementierung – Stop & Wait

    Einfaches Stop & Wait (Fenstergröße = 1) implementieren:

        Senden, auf ACK warten, Timeout

        select() für Intervalle verwenden

        Non-blocking Socket

    Hello/Close mit Bestätigung

    Timer-Logik implementieren

    Mit Server testen (fehlerfreier Fall)

## Go-Back-N erweitern

    Ringpuffer für Sendefenster implementieren

    Fenstermanagement (base, nextSeqNum)

    Kumulative ACKs verarbeiten

    Retransmission bei Timeout (alle unbestätigten Pakete)

    Fenstergrößen 1-10 unterstützen

    ACK-Verlust-Simulation (lossAck) integrieren

## Integration und Test

    Komplette Übertragungskette testen

    Verschiedene Szenarien testen:

        Fehlerfreie Übertragung

        Paketverluste

        ACK-Verluste

        Unterschiedliche Fenstergrößen

    Ausgaben für Nachvollziehbarkeit einbauen

    Fehlerbehandlung verbessern

## Teststrategie:

    Lokal auf einem Rechner testen

    Mit lossReq = 0.0 und lossAck = 0.0 beginnen

    Langsam Fehlerraten erhöhen

    Verschiedene Dateigrößen testen

    Edge Cases: leere Datei, große Dateien
