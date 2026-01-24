## Code für PlantUML (Client):

```
@startuml Client_GBN_StateMachine
' Simple Go-Back-N sender state machine, compatible PlantUML syntax

[*] --> INIT

INIT : initClient()
note right of INIT
  Variables:
  base, next_seqnum, packet_count,
  current_packet_num, send_buffer[], packet_timer[]
end note

INIT --> HELLO_SENT : arqSendHello()
HELLO_SENT : send HELLO / select(timeout)
HELLO_SENT --> IDLE : receive AnswHello / AnswOk
HELLO_SENT --> HELLO_RETRY : timeout
HELLO_RETRY --> HELLO_SENT : retry (max_retries)
HELLO_RETRY --> ERROR : max retries exceeded

IDLE : waiting for app calls
IDLE --> ENQUEUE_SEND : arqSendData(app_unit)
ENQUEUE_SEND --> WINDOW_FULL : packet_count >= window_size
ENQUEUE_SEND --> SEND_ON_INTERVAL : stored in send_buffer (packet_in_buffer=1)

WINDOW_FULL --> WAIT_INTERVAL : wait for ACK / interval
WAIT_INTERVAL --> RECEIVE_ACK : select readable
WAIT_INTERVAL --> INTERVAL_END : select timeout

SEND_ON_INTERVAL : send_packet(); start interval
SEND_ON_INTERVAL --> WAIT_INTERVAL

RECEIVE_ACK --> PROCESS_ACK
PROCESS_ACK : parse answ
PROCESS_ACK --> SLIDE_WINDOW : ack in window && ack > base
PROCESS_ACK --> IGNORE_ACK : ack outside window

SLIDE_WINDOW : free packets; base = ack_seq
SLIDE_WINDOW --> CHECK_IF_CONFIRMED

CHECK_IF_CONFIRMED --> SUCCESS_RETURN : if ack confirms this packet (ack_seq > req.SeNr)
CHECK_IF_CONFIRMED --> IDLE : else continue

IGNORE_ACK --> WAIT_INTERVAL

INTERVAL_END --> DECREMENT_TIMERS : decrement_all_timers()
DECREMENT_TIMERS --> CHECK_TIMEOUT
CHECK_TIMEOUT --> RETRANSMIT : if packet_timer[base] <= 0
CHECK_TIMEOUT --> NEXT_INTERVAL : else

RETRANSMIT : retransmit_all() (send window)
RETRANSMIT --> SEND_ON_INTERVAL
NEXT_INTERVAL --> SEND_ON_INTERVAL

SUCCESS_RETURN --> IDLE : return 0 to application

IDLE --> CLOSE_SENT : arqSendClose()
CLOSE_SENT : send CLOSE / select(timeout)
CLOSE_SENT --> FINISHED : receive AnswOk
CLOSE_SENT --> CLOSE_RETRY : timeout
CLOSE_RETRY --> CLOSE_SENT : retry (max)
CLOSE_RETRY --> ERROR : max retries exceeded

ERROR --> [*]
FINISHED --> [*]

@enduml
```

## Ausschreibung von Copilot für Zustandsdiagramm für den Client:
```
stateDiagram-v2
    [*] --> INIT : start
    INIT --> HELLO_SENT : arqSendHello()
    HELLO_SENT --> IDLE : HELLO-ACK (AnswHello/AnswOk)
    HELLO_SENT --> ERROR : hello timeout max retries

    IDLE --> ENQUEUE_SEND : app hat Zeile -> arqSendData()
    ENQUEUE_SEND --> SEND_ON_INTERVAL : packet enqueued
    SEND_ON_INTERVAL --> WAIT_INTERVAL : send_packet(); start/select interval
    WAIT_INTERVAL --> RECEIVE_ACK : select indicates readable -> receive_answer()
    WAIT_INTERVAL --> INTERVAL_END : select timeout (interval elapsed)
    RECEIVE_ACK --> PROCESS_ACK : AnswOk/AnswWarn/AnswErr
    PROCESS_ACK --> SLIDE_WINDOW : ack_seq in window && ack_seq > base / update base, packet_count
    SLIDE_WINDOW --> SUCCESS_RETURN : if ack confirms this packet
    RECEIVE_ACK --> IGNORE_ACK : ack outside window -> ignore
    IGNORE_ACK --> WAIT_INTERVAL : wait until interval end
    INTERVAL_END --> DECREMENT_TIMERS : decrement_all_timers()
    DECREMENT_TIMERS --> CHECK_TIMEOUT
    CHECK_TIMEOUT --> RETRANSMIT\n(if base timer <=0) : retransmit_all()
    CHECK_TIMEOUT --> NEXT_INTERVAL : no timeout
    RETRANSMIT --> SEND_ON_INTERVAL : retransmit immediately (counts as sending in interval)
    NEXT_INTERVAL --> SEND_ON_INTERVAL : if window still allows new send
    SUCCESS_RETURN --> IDLE : return success to application
    ENQUEUE_SEND --> WINDOW_FULL : if packet_count >= window_size
    WINDOW_FULL --> WAIT_INTERVAL : wait for ACKs/intervals (no enqueue)
    WINDOW_FULL --> IDLE : on ack freeing space, enqueue/send resumes

    IDLE --> CLOSE_SENT : arqSendClose()
    CLOSE_SENT --> FINISHED : CLOSE-ACK (AnswOk)
    CLOSE_SENT --> ERROR : close timeout max retries
    ERROR --> [*]
    FINISHED --> [*]
    ```

## Detaillierte textuelle Beschreibung — Client (Sender)

    Relevante Variablen
        base: Anfang des Sendefensters (erste unbestätigte SeqNr)
        next_seqnum: nächste SeqNr, die zur Einfügung in Sendepuffer vorgesehen ist (Zähler)
        packet_count: Anzahl unbestätigter Pakete im Fenster
        window_size: gewünschte Fenstergröße (1..10)
        send_buffer[]: Ringpuffer mit gespeicherten Request-Paketen
        packet_timer[]: Timer (in Intervallen) pro Pufferposition (Timeout = GBN_TIMEOUT_UNITS * GBN_TIMEOUT_INT_MS)
        packet_sent[]: Flag, ob Pufferposition aktiv ist
        current_packet_num: Sequenznummer, die gerade für neues app_unit verwendet wird

    Zustände & Aktionen

        INIT
            Aktion: initClient() aufrufen, Socket non-blocking erzeugen
            Übergang bei arqSendHello -> HELLO_SENT

        HELLO_SENT
            Aktion: sende HELLO-Paket, warte mit select() maximal GBN_TIMEOUT_UNITS*GBN_TIMEOUT_INT_MS
            bei AnswHello/AnswOk -> set connection_active = 1, base=0,next_seqnum=0 -> IDLE
            bei wiederholten Timeouts -> Fehler

        IDLE
            Aktion: Anwendung ruft arqSendData() mit einer Zeile
            Übergang zu ENQUEUE_SEND

        ENQUEUE_SEND
            Aktion: falls packet_count < window_size, speichere Paket in send_buffer[idx = SeNr % B_SIZE], set packet_timer[idx]=GBN_TIMEOUT_UNITS, packet_sent[idx]=1, packet_count++, next_seqnum++.
            Andernfalls: WINDOW_FULL (warte auf ACKs)

        SEND_ON_INTERVAL
            Aktion: einmal pro Intervall maximal ein Paket senden (neu oder bei Retransmit das erste im Fenster). Nach Senden: enter WAIT_INTERVAL

        WAIT_INTERVAL (select für GBN_TIMEOUT_INT_MS)
            Wenn Antwort vor Ablauf eintrifft: PROCESS_ACK
            Wenn Timeout (kein ACK): INTERVAL_END -> decrement timers -> eventuell Retransmit

        PROCESS_ACK
            Prüfe ans->AnswType:
                AnswOk: ack_seq = SeNo
                    Wenn ack_seq in [base, next_seqnum]: advance base to ack_seq, free packets (packet_sent[...] = 0), packet_count -= freed
                    Wenn ack_seq > req.SeNr: bestätigen, return success to application
                AnswWarn/Err: Fehler/Abbruch
                AnsW außerhalb window: ignorieren
            Nach Verarbeitung warte bis Intervallende (falls ACK kam vor Intervallende) — d.h. Ruhe bis Intervallende und dann weiter

        TIMEOUT / RETRANSMIT
            Wenn packet_timer[base] <= 0: retransmit_all(): set packet_timer[...] = GBN_TIMEOUT_UNITS, sende alle unbestätigten Pakete (go-back-n)

        CLOSE_SENT
            Sende CLOSE, warte auf AnswOk; Retry mit select() bis max retries -> FINISHED oder ERROR

    Wichtige Regeln (Mapping auf Verhalten/Aufgabe):
        Pro Intervall maximal 1 send operation (neu oder retransmit) — der select() mit GBN_TIMEOUT_INT_MS bildet das Intervall.
        Timer werden in Einheiten (Intervallen) dekrementiert.
        ACKs sind kumulativ: AnswOk.SeNo = nächstes erwartetes SeqNr; Client verschiebt base entsprechend.
        Ringpuffer: idx = SeNr % GBN_BUFFER_SIZE
        Bei Timeout des ersten Pakets: Retransmit aller unbestätigten Pakete (Go-Back-N).


