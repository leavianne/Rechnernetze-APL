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