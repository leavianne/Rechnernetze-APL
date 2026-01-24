#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>
#include <time.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

/* --------------------------------------------------------------- */
/*  Globale Variablen - Zustand des GBN-Senders                    */
/* --------------------------------------------------------------- */

static int sockfd = -1;                     // UDP Socket-Deskriptor
static struct sockaddr_in6 server_addr;     // Server-Adresse (IPv6)
static socklen_t server_addr_len = sizeof(server_addr);

// Go-Back-N Fenster-Verwaltung
static unsigned long base = 0;              // Anfang des Sendefensters (erste unbestätigte SeqNr)
static unsigned long next_seqnum = 0;       // Nächste zu verwendende Sequenznummer
static int window_size = 1;                 // Aktuelle Fenstergröße (1..10)
static int packet_count = 0;                // Anzahl der Pakete aktuell im Fenster

// Ringpuffer für unbestätigte Pakete
static struct request send_buffer[GBN_BUFFER_SIZE]; // Ringpuffer: speichert alle unbestätigten Pakete
static int packet_timer[GBN_BUFFER_SIZE];   // Timer pro Paket (in Intervallen)
static int packet_sent[GBN_BUFFER_SIZE];    // Flag: ob Paket gesendet wurde

// Verbindungsstatus
static int connection_active = 0;               // Flag: ist die Verbindung aktiv?
static unsigned long current_packet_num = 0;    // Zähler für Datenpakete (Sequenznummern)

/* --------------------------------------------------------------- */
/*  Initialisierung / Abschluss                                    */
/* --------------------------------------------------------------- */

/**
 * initClient() - Initialisiert den UDP-Client
 * 
 * Aufgaben:
 * - Serveradresse auflösen (DNS mit getaddrinfo)
 * - UDP-Socket (IPv6) erstellen
 * - Socket auf NON-BLOCKING setzen (Wichtig für select())
 * - GBN-Statusvariablen zurücksetzen
 * 
 * Parameter:
 * - name: Servername oder IP-Adresse (NULL = localhost ::1)
 * - port: Portnummer als String (z.B. "3333")
 */
void initClient(char *name, const char *port)
{
    struct addrinfo hints, *res, *p;
    int status;
    
    // Hinweise für getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;         // Nur IPv6
    hints.ai_socktype = SOCK_DGRAM;     // UDP (datagram)
    
    // Defaultwert falls name == NULL
    if (name == NULL) {
        name = "::1";   // IPv6 loopback = localhost
    }
    
    // DNS-Auflösung: name + port -> addrinfo struktur
    if ((status = getaddrinfo(name, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }
    
    // Versuche Socket mit jeder der zurückgegebenen Adressen zu erstellen
    for (p = res; p != NULL; p = p->ai_next) {
        // Socket erstellen
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;   // Nächste Adresse versuchen
        }
        
        // WICHTIG: Socket auf NON-BLOCKING setzen
        // Damit blockiert recvfrom() nicht und wir können select() verwenden
        fcntl(sockfd, F_SETFL, O_NONBLOCK);

        // Serveradresse speichern für später (sendto)
        memcpy(&server_addr, p->ai_addr, p->ai_addrlen);
        server_addr_len = p->ai_addrlen;
        break;
    }
    
    // Speicher freigeben (getaddrinfo allokiert Speicher)
    freeaddrinfo(res);
    
    if (p == NULL) {
        fprintf(stderr, "Failed to create socket\n");
        exit(EXIT_FAILURE);
    }
    
    // GBN-Zustand zurücksetzen
    base = 0;
    next_seqnum = 0;
    packet_count = 0;
    connection_active = 0;
    current_packet_num = 0;
    
    // Buffer initialisieren
    memset(send_buffer, 0, sizeof(send_buffer));
    memset(packet_timer, 0, sizeof(packet_timer));
    memset(packet_sent, 0, sizeof(packet_sent));
    
    printf("[CLIENT] Initialized (server=%s, port=%s)\n", name, port);
}

/**
 * closeClient() - Cleanup beim Beenden des Clients
 * 
 * Aufgaben:
 * - Socket schließen
 * - Flaggen zurücksetzen
 */
void closeClient(void)
{
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
    connection_active = 0;
    printf("[CLIENT] Closed\n");
}

/* --------------------------------------------------------------- */
/*  Interne Hilfsfunktionen - UDP Senden/ Empfangen                */
/* --------------------------------------------------------------- */

/**
 * send_packet() - Sendet ein Packet via UDP
 * 
 * Benutzt: sendto() mit dem non-blocking Socket
 */
static int send_packet(struct request *req)
{
    ssize_t n;
    
    // sendto(): UDP -> Serveradresse (ohne Verbindung)
    n = sendto(sockfd, req, sizeof(struct request), 0,
               (struct sockaddr *)&server_addr, server_addr_len);
    
    if (n < 0) {
        perror("sendto");
        return -1;
    }
    
    // Debug-Ausgabe
    printf("[CLIENT] SEND: type=%c seq=%lu len=%lu\n",
           req->ReqType, req->SeNr, req->FlNr);
    return 0;
}

/**
 * receive_answer() - Empfängt eine Antwort (non-blocking)
 * 
 * Da Socket non-blocking ist, gibt recvfrom() sofort NULL zurück,
 * wenn keine Daten verfügbar sind (EWOULDBLOCK).
 */
static struct answer *receive_answer(void)
{
    static struct answer ans;
    ssize_t n;
    
    // Non-blocking recvfrom(): Gibt sofort zurück wenn keine Daten
    n = recvfrom(sockfd, &ans, sizeof(ans), 0, NULL, NULL);
    
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return NULL;  // Keine Daten verfügbar (erwartet)
        }
        perror("recvfrom");
        return NULL;
    }
    
    if (n > 0) {
        printf("[CLIENT] RECV: type=%c seq/err=%lu\n",
               ans.AnswType, ans.SeNo);
        return &ans;
    }
    
    return NULL;
}

/**
 * decrement_all_timers() - Dekrement Timer für alle Pakete im Fenster
 * 
 * Wird aufgerufen nach jedem Intervall (100ms)
 * Timer sind in "Intervallen" gezählt:
 * - GBN_TIMEOUT_UNITS = 3 -> Timeout nach 3 Intervallen (300ms)
 */
static void decrement_all_timers(void)
{
    // Gehe durch alle Pakete im aktuellen Fenster
    for (int i = 0; i < packet_count; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE; // Ringpuffer-Index

        // Wenn Paket gespeichert und Timer > 0: dekrementiere
        if (packet_sent[idx] && packet_timer[idx] > 0) {
            packet_timer[idx]--;
            printf("[CLIENT] TIMER: packet[%d] (seq=%lu) = %d intervals\n",
                   idx, send_buffer[idx].SeNr, packet_timer[idx]);
        }
    }
}

/**
 * has_timeout() - Prüft ob erstes Paket im Fenster Timeout hat
 * 
 * Go-Back-N: Nur das erste Paket (base) hat einen Timer.
 * Wenn Timer <= 0 -> Timeout!
 */
static int has_timeout(void)
{
    if (packet_count > 0) {
        int idx = base % GBN_BUFFER_SIZE;   // Erstes Paket

        // Paket existiert und Timer abgelaufen?
        if (packet_sent[idx] && packet_timer[idx] <= 0) {
            printf("[CLIENT] TIMEOUT: packet seq=%lu timer expired\n",
                   send_buffer[idx].SeNr);
            return 1;
        }
    }
    return 0;
}

/**
 * retransmit_all() - Sendet alle Pakete im Fenster erneut (Go-Back-N)
 * 
 * Hier ist Go-Back-N:
 * - Bei Timeout des ersten Pakets (base)
 * - Sende ALLE unbestätigten Pakete erneut
 * - Timer werden zurückgesetzt
 */
static int retransmit_all(void)
{
    printf("[CLIENT] RETRANSMIT: Sending all packets in window (base=%lu, count=%d)\n",
           base, packet_count);
    
    // Gehe durch alle Pakete im Fenster
    for (int i = 0; i < packet_count; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE;
        if (packet_sent[idx]) {
            // Timer zurücksetzen
            packet_timer[idx] = GBN_TIMEOUT_UNITS;
            
            // Paket erneut senden
            if (send_packet(&send_buffer[idx]) < 0) {
                fprintf(stderr, "[CLIENT] Retransmit failed for packet %lu\n",
                        send_buffer[idx].SeNr);
                return -1;
            }
        }
    }
    return 0;
}

/* --------------------------------------------------------------- */
/*  Hauptfunktionen - ARQ-API für Anwendung                        */
/* --------------------------------------------------------------- */

/**
 * arqSendHello() - Verbindungsaufbau (Handshake)
 * 
 * Aufgaben:
 * 1. HELLO-Paket senden
 * 2. Mit select() auf HELLO-ACK warten (mit Timeout)
 * 3. Bei Timeout: retry (max 10 mal)
 * 4. Fenster-Zustand initialisieren bei Erfolg
 */
int arqSendHello(int winSize)
{
    struct request req;
    struct answer *ans;
    int retry_count = 0;
    const int max_retries = 10;
    struct timeval tv;
    fd_set readfds;
    
    // HELLO-Paket vorbereiten
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqHello;
    req.SeNr = 0;
    req.FlNr = 0;
    
    window_size = winSize;
    printf("[CLIENT] HELLO: Sending with window=%d\n", winSize);
    
    // Mit Retry-Schleife
    while (retry_count < max_retries) {
        if (send_packet(&req) < 0) {
            return -1;
        }
        
        // Mit select() auf Antwort warten (Timeout: 300ms)
        tv.tv_sec = (GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS) / 1000;
        tv.tv_usec = ((GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS) % 1000) * 1000;
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            // Socket ist lesbar -> ACK verfügbar
            ans = receive_answer();
            if (ans != NULL && (ans->AnswType == AnswHello || ans->AnswType == AnswOk)) {
                // HELLO-ACK empfangen
                connection_active = 1;
                base = 0;
                next_seqnum = 0;
                packet_count = 0;
                current_packet_num = 0;
                printf("[CLIENT] HELLO: Acknowledged (ACK type=%c)\n", ans->AnswType);
                return 0; // Erfolg
            }
        } else if (ret < 0) {
            perror("select");
            return -1;
        }
        // ret == 0: select timeout -> retry

        retry_count++;
        printf("[CLIENT] HELLO: Timeout, retry %d/%d\n", retry_count, max_retries);
    }
    
    fprintf(stderr, "[CLIENT] HELLO: No response after %d retries\n", max_retries);
    return -1; // FEHLER
}

/**
 * arqSendData() - Sendet ein Datenpaket (zuverlässig)
 * 
 * Implementiert den Go-Back-N Algorithmus mit Intervallen (100ms)
 * 
 * Pro Intervall in Reihenfolge:
 * Prüfe ob Timeout vorliegt -> Retransmit
 * Füge neues Paket ins Fenster ein (falls Platz)
 * Sende 1 Paket
 * Warte auf ACK oder Timeout
 * Dekrement Timer
 */
int arqSendData(const struct app_unit *app, int winSize)
{
    struct request req;
    struct answer *ans;
    int interval_count = 0;
    const int max_intervals = 50;
    struct timeval tv;
    fd_set readfds;
    
    if (!connection_active) {
        fprintf(stderr, "[CLIENT] ERROR: No active connection\n");
        return -1;
    }
    
    if (!app || app->len <= 0) {
        fprintf(stderr, "[CLIENT] ERROR: Invalid app unit\n");
        return -1;
    }
    
    // Request vorbereiten
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqData;
    req.SeNr = current_packet_num;
    
    size_t copy_len = app->len;
    if (copy_len >= BufferSize) {
        copy_len = BufferSize - 1;
    }
    
    memcpy(req.name, app->data, copy_len);
    req.name[copy_len] = '\0';
    req.FlNr = copy_len;
    
    printf("[CLIENT] DATA: Preparing packet seq=%lu (len=%lu)\n", 
           current_packet_num, req.FlNr);
    
    /* ============================================================ */
    /* ECHTES Go-Back-N: Fenster kann mehrere Pakete halten!        */
    /* ============================================================ */
    
    int packet_in_buffer = 0;
    
    while (interval_count < max_intervals) {
        printf("[CLIENT] --- INTERVAL %d START (window=%d/%d, base=%lu, next=%lu) ---\n",
               interval_count, packet_count, winSize, base, next_seqnum);
        
        // 1. TIMEOUT PRÜFEN und RETRANSMIT
        if (has_timeout()) {
            printf("[CLIENT] TIMEOUT at interval %d\n", interval_count);
            if (retransmit_all() < 0) {
                return -1;
            }
        }
        
        // 2. NEUES PAKET INS FENSTER (falls Platz und noch nicht drin)
        if (!packet_in_buffer && packet_count < winSize) {
            int idx = req.SeNr % GBN_BUFFER_SIZE;
            memcpy(&send_buffer[idx], &req, sizeof(struct request));
            packet_sent[idx] = 1;
            packet_timer[idx] = GBN_TIMEOUT_UNITS;
            packet_count++;
            next_seqnum++;
            packet_in_buffer = 1;
            
            printf("[CLIENT] ENQUEUE: packet seq=%lu into buffer[%d], window now %d/%d\n",
                   req.SeNr, idx, packet_count, winSize);
        }
        
        // 3. SENDE ALLE PAKETE IM FENSTER (echtes Go-Back-N!)
        for (int i = 0; i < packet_count; i++) {
            int send_idx = (base + i) % GBN_BUFFER_SIZE;
            if (packet_sent[send_idx]) {
                if (send_packet(&send_buffer[send_idx]) < 0) {
                    return -1;
                }
            }
        }
        
        // 4. WARTE AUF ACK (100ms)
        tv.tv_sec = GBN_TIMEOUT_INT_MS / 1000;
        tv.tv_usec = (GBN_TIMEOUT_INT_MS % 1000) * 1000;
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            ans = receive_answer();
            if (ans != NULL && ans->AnswType == AnswOk) {
                unsigned long ack_seq = ans->SeNo;
                
                printf("[CLIENT] ACK RECEIVED: ack_seq=%lu\n", ack_seq);
                
                // FENSTER VERSCHIEBEN (kumulativ!)
                if (ack_seq > base && ack_seq <= next_seqnum) {
                    int packets_to_free = ack_seq - base;
                    printf("[CLIENT] WINDOW ADVANCE: base %lu -> %lu (free %d packets)\n",
                           base, ack_seq, packets_to_free);
                    
                    for (int i = 0; i < packets_to_free && i < packet_count; i++) {
                        int free_idx = (base + i) % GBN_BUFFER_SIZE;
                        packet_sent[free_idx] = 0;
                        packet_timer[free_idx] = 0;
                    }
                    
                    packet_count -= packets_to_free;
                    if (packet_count < 0) packet_count = 0;
                    base = ack_seq;
                }
                
                // IST UNSER PAKET BESTÄTIGT?
                if (ack_seq > req.SeNr) {
                    printf("[CLIENT] CONFIRMED: packet seq=%lu confirmed\n", req.SeNr);
                    current_packet_num++;
                    return 0;
                }
            }
        } else if (ret < 0) {
            perror("select");
            return -1;
        }
        
        // 5. TIMER DEKREMENTIEREN
        decrement_all_timers();
        
        printf("[CLIENT] --- INTERVAL %d END ---\n\n", interval_count);
        interval_count++;
    }
    
    fprintf(stderr, "[CLIENT] ERROR: Max intervals exceeded for packet %lu\n", 
            current_packet_num);
    return -3;
}

/**
 * arqSendClose() - Verbindungsabbau (Handshake)
 * 
 * Ähnlich wie Hello, aber mit ReqClose-Paket
 */
int arqSendClose(int winSize)
{
    struct request req;
    struct answer *ans;
    int retry_count = 0;
    const int max_retries = 10;
    struct timeval tv;
    fd_set readfds;
    
    if (!connection_active) {
        fprintf(stderr, "[CLIENT] ERROR: No active connection\n");
        return -1;
    }
    
    // CLOSE-Paket vorbereiten
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqClose;
    req.SeNr = current_packet_num;
    req.FlNr = 0;
    
    printf("[CLIENT] CLOSE: Sending close (seq=%lu)\n", current_packet_num);
    
    // Mit Retry-Schleife
    while (retry_count < max_retries) {
        if (send_packet(&req) < 0) {
            return -1;
        }
        
        // Mit select() auf Antwort warten
        tv.tv_sec = (GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS) / 1000;
        tv.tv_usec = ((GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS) % 1000) * 1000;
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            ans = receive_answer();
            if (ans != NULL && ans->AnswType == AnswOk) {
                connection_active = 0;
                printf("[CLIENT] CLOSE: Acknowledged\n");
                return 0;   // Erfolg
            }
        } else if (ret < 0) {
            perror("select");
            return -1;
        }
        
        retry_count++;
        printf("[CLIENT] CLOSE: Timeout, retry %d/%d\n", retry_count, max_retries);
    }
    
    fprintf(stderr, "[CLIENT] CLOSE: No response after %d retries\n", max_retries);
    return -1;  // Fehler
}