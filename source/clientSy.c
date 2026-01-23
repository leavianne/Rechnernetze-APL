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

/* Globale Variablen */
static int sockfd = -1;
static struct sockaddr_in6 server_addr;
static socklen_t server_addr_len = sizeof(server_addr);

/* GBN-Zustandsvariablen */
static unsigned long base = 0;          /* Anfang des Sendefensters */
static unsigned long next_seqnum = 0;   /* Nächste zu verwendende SeqNr */
static int window_size = 1;             /* Aktuelle Fenstergröße */
static int packet_count = 0;            /* Anzahl unbestätigter Pakete */

/* Ringpuffer für Sendefenster */
static struct request send_buffer[GBN_BUFFER_SIZE];
static int packet_timers[GBN_BUFFER_SIZE];  /* Timer in Intervallen */
static int packet_sent[GBN_BUFFER_SIZE];    /* Paket wurde gesendet */

/* Statische Variablen für die aktuelle Übertragung */
static int connection_active = 0;
static unsigned long current_packet_num = 0;

/* --------------------------------------------------------------- */
/*  Initialisierung / Abschluss                                   */
/* --------------------------------------------------------------- */

void initClient(char *name, const char *port) 
{
    struct addrinfo hints, *res, *p;
    int status;
    
    /* Hints für getaddrinfo */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;       /* IPv6 */
    hints.ai_socktype = SOCK_DGRAM;   /* UDP */
    
    /* Standardwert für name */
    if (name == NULL) {
        name = "::1";  /* IPv6 loopback */
    }
    
    /* Adresse auflösen */
    if ((status = getaddrinfo(name, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }
    
    /* Passende Adresse finden */
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }
        
        /* Socket non-blocking setzen */
        fcntl(sockfd, F_SETFL, O_NONBLOCK);
        
        /* Serveradresse speichern */
        memcpy(&server_addr, p->ai_addr, p->ai_addrlen);
        server_addr_len = p->ai_addrlen;
        
        break;
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        fprintf(stderr, "Failed to create socket\n");
        exit(EXIT_FAILURE);
    }
    
    /* GBN-Zustand initialisieren */
    base = 0;
    next_seqnum = 0;
    packet_count = 0;
    connection_active = 0;
    current_packet_num = 0;
    
    memset(send_buffer, 0, sizeof(send_buffer));
    memset(packet_timers, 0, sizeof(packet_timers));
    memset(packet_sent, 0, sizeof(packet_sent));
    
    printf("Client: Initialized (server=%s, port=%s)\n", name, port);
}

void closeClient(void) 
{
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
    
    connection_active = 0;
    printf("Client: Closed\n");
}

/* --------------------------------------------------------------- */
/*  Interne Hilfsfunktionen                                        */
/* --------------------------------------------------------------- */

static int send_packet(struct request *req) 
{
    ssize_t n;
    
    n = sendto(sockfd, req, sizeof(struct request), 0,
               (struct sockaddr *)&server_addr, server_addr_len);
    
    if (n < 0) {
        perror("sendto");
        return -1;
    }
    
    printf("Client: Sent packet (type=%c, seq=%lu, len=%lu)\n",
           req->ReqType, req->SeNr, req->FlNr);
    return 0;
}

static struct answer *receive_answer(int timeout_ms) 
{
    static struct answer ans;
    fd_set readfds;
    struct timeval tv;
    ssize_t n;
    int ret;
    
    /* File Descriptor Set vorbereiten */
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    
    /* Timeout setzen */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    /* Auf Daten warten */
    ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
    
    if (ret > 0) {
        if (FD_ISSET(sockfd, &readfds)) {
            /* Antwort empfangen */
            n = recvfrom(sockfd, &ans, sizeof(ans), 0, NULL, NULL);
            if (n > 0) {
                if (n < (ssize_t)sizeof(ans.AnswType) + sizeof(ans.FlNr) + sizeof(ans.SeNo)) {
                    fprintf(stderr, "Client: Received incomplete answer\n");
                    return NULL;
                }
                printf("Client: Received answer (type=%c, seq/err=%lu)\n",
                       ans.AnswType, ans.SeNo);
                return &ans;
            }
        }
    }
    
    return NULL; /* Timeout oder Fehler */
}

static void update_timers(int interval_units) 
{
    for (int i = 0; i < GBN_BUFFER_SIZE; i++) {
        if (packet_timers[i] > 0) {
            packet_timers[i] -= interval_units;
            if (packet_timers[i] <= 0 && packet_sent[i]) {
                /* Timeout für dieses Paket */
                printf("Client: Timeout for packet %lu\n", send_buffer[i].SeNr);
            }
        }
    }
}

static int has_timeout(void) 
{
    for (int i = 0; i < GBN_BUFFER_SIZE; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE;
        if (packet_timers[idx] <= 0 && packet_sent[idx]) {
            return 1; /* Timeout gefunden */
        }
    }
    return 0;
}

static void retransmit_window(int winSize) 
{
    printf("Client: Retransmitting window (base=%lu, count=%d)\n", base, packet_count);
    
    for (int i = 0; i < winSize && i < packet_count; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE;
        if (packet_sent[idx]) {
            /* Timer zurücksetzen und erneut senden */
            packet_timers[idx] = GBN_TIMEOUT_UNITS;
            if (send_packet(&send_buffer[idx]) < 0) {
                fprintf(stderr, "Client: Retransmission failed for packet %lu\n",
                        send_buffer[idx].SeNr);
            } else {
                printf("Client: Retransmitted packet %lu\n", send_buffer[idx].SeNr);
            }
        }
    }
}

/* --------------------------------------------------------------- */
/*  Haupt-ARQ-Funktion (doRequest)                                 */
/* --------------------------------------------------------------- */

static struct answer *doRequest(struct request *req, int winSize, 
                                int *windowFull, int *retransmission)
{
    struct answer *ans = NULL;
    int timeout_count = 0;
    int max_retries = 3;
    int needs_retransmit = 0;
    
    /* Parameter initialisieren */
    if (windowFull) *windowFull = 0;
    if (retransmission) *retransmission = 0;
    
    /* Fenster voll? */
    if (packet_count >= winSize) {
        if (windowFull) *windowFull = 1;
        printf("Client: Window full (%d/%d)\n", packet_count, winSize);
        struct answer *ans = receive_answer(50);
        if (ans != NULL) {
            // ACK könnte Fenster freimachen
            return ans;
        }
        return NULL;
    }
    
    /* Data-Paket in Puffer speichern */
    if (req->ReqType == ReqData) {
        int idx = next_seqnum % GBN_BUFFER_SIZE;
        
        /* Altes Paket überschreiben */
        memcpy(&send_buffer[idx], req, sizeof(struct request));
        packet_timers[idx] = GBN_TIMEOUT_UNITS;
        packet_sent[idx] = 1;
        
        next_seqnum++;
        packet_count++;
        
        printf("Client: Stored packet %lu in buffer[%d] (count=%d)\n",
               req->SeNr, idx, packet_count);
    }
    
    /* Paket senden */
    if (send_packet(req) < 0) {
        return NULL;
    }
    
    /* Auf Antwort warten */
    while (timeout_count < max_retries) {
        /* Timer aktualisieren */
        update_timers(1); /* 1 Intervall */
        
        /* Auf Antwort warten (maximal 1 Intervall) */
        ans = receive_answer(GBN_TIMEOUT_INT_MS);
        
        if (ans != NULL) {
            /* Antwort empfangen */
            if (ans->AnswType == AnswErr || ans->AnswType == AnswWarn) {
                printf("Client: Received error/warning (err=%lu)\n", ans->ErrNo);
                return ans;
            }
            break; /* Erfolgreiche Antwort */
        }
        
        /* Timeout: Prüfen ob Retransmission nötig */
        needs_retransmit = has_timeout();
        
        if (needs_retransmit) {
            if (retransmission) *retransmission = 1;
            retransmit_window(winSize);
            timeout_count++;
        } else {
            /* Kein Timeout, weiter warten */
            usleep(GBN_TIMEOUT_INT_MS * 1000);
        }
    }
    
    if (ans == NULL) {
        printf("Client: No response after %d retries\n", max_retries);
        return NULL;
    }
    
    /* Fenster basierend auf kumulativem ACK verschieben */
    if (ans->AnswType == AnswOk || ans->AnswType == AnswHello) {
        unsigned long ack_seq = ans->SeNo;
        
        if (ack_seq > base) {
            int packets_to_free = ack_seq - base;
            
            printf("Client: Advancing window (%lu -> %lu), freeing %d packets\n",
                   base, ack_seq, packets_to_free);
            
            /* Alte Pakete aus Puffer entfernen */
            for (int i = 0; i < packets_to_free && i < packet_count; i++) {
                int idx = (base + i) % GBN_BUFFER_SIZE;
                packet_sent[idx] = 0;
                packet_timers[idx] = 0;
            }
            
            packet_count -= packets_to_free;
            if (packet_count < 0) packet_count = 0;
            base = ack_seq;
        }
    }
    
    return ans;
}

/* --------------------------------------------------------------- */
/*  Externe ARQ-API (Wrapper um doRequest)                         */
/* --------------------------------------------------------------- */

int arqSendHello(int winSize) 
{
    struct request req;
    struct answer *ans;
    
    /* Request vorbereiten */
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqHello;
    req.SeNr = 0;
    req.FlNr = 0;
    
    /* Fenstergröße speichern */
    window_size = winSize;
    
    printf("Client: Sending Hello (window=%d)\n", winSize);
    
    /* Hello senden und auf Antwort warten */
    ans = doRequest(&req, winSize, NULL, NULL);
    
    if (ans == NULL) {
        fprintf(stderr, "Client: No response to Hello\n");
        return -1;
    }
    
    if (ans->AnswType != AnswHello && ans->AnswType != AnswOk) {
        fprintf(stderr, "Client: Invalid response to Hello: %c\n", ans->AnswType);
        return -2;
    }
    
    connection_active = 1;
    printf("Client: Hello acknowledged\n");
    return 0;
}

int arqSendData(const struct app_unit *app, int winSize) 
{
    struct request req;
    struct answer *ans;
    int windowFull = 0;
    int retransmission = 0;
    int retry_count = 0;
    
    if (!connection_active) {
        fprintf(stderr, "Client: No active connection\n");
        return -1;
    }
    
    if (!app || app->len <= 0) {
        fprintf(stderr, "Client: Invalid app unit\n");
        return -1;
    }
    
    /* Request vorbereiten */
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqData;
    req.SeNr = current_packet_num;
    
    /* Daten kopieren (begrenzt auf BufferSize) */
    size_t copy_len = app->len;
    if (copy_len >= BufferSize) {
        copy_len = BufferSize - 1;
        fprintf(stderr, "Client: Data truncated to %zu bytes\n", copy_len);
    }
    
    memcpy(req.name, app->data, copy_len);
    req.name[copy_len] = '\0';
    req.FlNr = copy_len;
    
    printf("Client: Sending Data packet %lu (len=%lu)\n", 
           current_packet_num, req.FlNr);
    
    /* Paket senden mit GBN-Logik */
    do {
        ans = doRequest(&req, winSize, &windowFull, &retransmission);
        
        if (retransmission) {
            printf("Client: Retransmission needed, retry %d\n", retry_count + 1);
            retry_count++;
            if (retry_count > 3) {
                fprintf(stderr, "Client: Too many retransmissions for packet %lu\n",
                        current_packet_num);
                return -1;
            }
            continue;
        }
        
        if (windowFull) {
            /* Fenster voll, warten */
            printf("Client: Window full, waiting...\n");
            usleep(GBN_TIMEOUT_INT_MS * 1000);
            continue;
        }
        
        break;
    } while (1);
    
    if (ans == NULL) {
        fprintf(stderr, "Client: No response for packet %lu\n", current_packet_num);
        return -3;
    }
    
    if (ans->AnswType == AnswErr || ans->AnswType == AnswWarn) {
        fprintf(stderr, "Client: Error/Warning for packet %lu: %lu\n",
                current_packet_num, ans->ErrNo);
        return -4;
    }
    
    /* Nächste Paketnummer */
    current_packet_num++;
    return 0;
}

int arqSendClose(int winSize) 
{
    struct request req;
    struct answer *ans;
    
    if (!connection_active) {
        fprintf(stderr, "Client: No active connection\n");
        return -1;
    }
    
    /* Request vorbereiten */
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqClose;
    req.SeNr = current_packet_num;
    req.FlNr = 0;
    
    printf("Client: Sending Close (seq=%lu)\n", current_packet_num);
    
    /* Close senden */
    ans = doRequest(&req, winSize, NULL, NULL);
    
    if (ans == NULL) {
        fprintf(stderr, "Client: No response to Close\n");
        return -1;
    }
    
    if (ans->AnswType != AnswOk) {
        fprintf(stderr, "Client: Invalid response to Close: %c\n", ans->AnswType);
        return -2;
    }
    
    connection_active = 0;
    printf("Client: Close acknowledged\n");
    return 0;
}
