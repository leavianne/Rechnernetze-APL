## Code für PlantUML (Server):
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
## Ausschreibung von Copilot für Zustandsdiagramm für den Server:
```
stateDiagram-v2
    [*] --> LISTEN : arqServerLoop() start
    LISTEN --> GOT_REQUEST : select() readable -> getRequest()
    GOT_REQUEST --> DISCARD_PACKET : simulated lossReq -> drop
    GOT_REQUEST --> PROCESS_HELLO : ReqHello
    GOT_REQUEST --> PROCESS_DATA : ReqData
    GOT_REQUEST --> PROCESS_CLOSE : ReqClose
    PROCESS_HELLO --> START_APP : appStartCallback()
    START_APP --> SEND_HELLO_ACK : set transferActive=1
    SEND_HELLO_ACK --> LISTEN : sendAnswer(AnswHello)
    PROCESS_DATA --> NOT_ACTIVE : if !transferActive -> AnswErr
    PROCESS_DATA --> IN_ORDER : if SeNr == nextExpected
    PROCESS_DATA --> DUPLICATE : if SeNr < nextExpected
    PROCESS_DATA --> OUT_OF_ORDER : if SeNr > nextExpected
    IN_ORDER --> WRITE_APP : appWriteCallback()
    WRITE_APP --> SEND_OK_ACK : nextExpected++; AnswOk.SeNo = nextExpected
    DUPLICATE --> SEND_OK_ACK
    OUT_OF_ORDER --> SEND_WARN : AnswWarn(ERR_WRONG_SEQ)
    SEND_OK_ACK --> MAY_DROP_ACK : simulate lossAck? -> drop ack
    SEND_OK_ACK --> SEND_ANSWER : sendAnswer(answ)
    SEND_ANSWER --> LISTEN
    PROCESS_CLOSE --> END_APP : appEndCallback(); AnswOk.SeNo = nextExpected
    END_APP --> SEND_ANSWER
    NOT_ACTIVE --> SEND_ANSWER
    DISCARD_PACKET --> LISTEN
    SEND_ANSWER --> LISTEN
    LISTEN --> [*] : on Close & AnswOk -> exitServer()
```
  
## Detaillierte textuelle Beschreibung — Server (Empfänger)

### Relevante Variablen
nextExpected: Nummer des nächsten erwarteten Pakets (initial 0)
transferActive: Flag, ob Anwendung (Datei) offen und Transfer aktiv ist
appStartCallback, appWriteCallback, appEndCallback: Funktionspointer zur Anwendung (server.c)

## Zustände & Aktionen

### LISTEN
Aktion: select() mit kurzem Timeout (100ms) auf UDP-Paket  
Bei Paket: getRequest() -> GOT_REQUEST  

### GOT_REQUEST -> processRequest()
Simuliere Paketverlust (lossReq): ggf. return NULL -> Paket verworfen  

### Je nach ReqType:  

### ReqHello:
set nextExpected = 0; rufe appStartCallback() (öffnet Datei)  
set transferActive = 1 (bei Erfolg)  
Antw.: AnswHello, SeNo = 0  

### ReqData:
Wenn !transferActive -> AnswErr(ERR_ILLEGAL_REQUEST)  
Wenn SeNr == nextExpected:  
rufe appWriteCallback(buf, len)  
nextExpected++  
Antw.: AnswOk, SeNo = nextExpected (kumulativ)  
Wenn SeNr < nextExpected:  
Duplikat -> Antw.: AnswOk, SeNo = nextExpected  
Wenn SeNr > nextExpected:  
Out-of-order -> Antw.: AnswWarn, ErrNo = ERR_WRONG_SEQ (Paket verworfen) 

### ReqClose:
Falls transferActive: appEndCallback() (Datei schließen), transferActive=0  
Antw.: AnswOk, SeNo = nextExpected  

### SEND_ANSWER
Simuliere ACK-Verlust (lossAck): ggf. nicht senden  
Sonst: sendAnswer(answPtr)  

### CLOSE_HANDSHAKE
Wenn ReqClose empfangen und AnswOk gesendet: beende Loop und exitServer()  

## Wichtige Regeln:
Empfänger puffert NICHT (keine Reassembly für OOO) — nur in-order akzeptieren.  
Antworten sind kumulativ: SeNo = nächstes erwartetes Paket.  
Bei Duplicate sendet Server das aktuelle kumulative ACK (nextExpected).  
Loss-Simulationen mit lossReq / lossAck für Tests.  
