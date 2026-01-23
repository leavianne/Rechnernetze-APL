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
static unsigned long base = 0;              /* Anfang des Fensters */
static unsigned long next_seqnum = 0;       /* Nächste zu sendende SeqNr */
static int window_size = 1;                 /* Aktuelle Fenstergröße */
static int packet_count = 0;                /* Anzahl unbestätigter Pakete */

/* Ringpuffer für Sendefenster */
static struct request send_buffer[GBN_BUFFER_SIZE];
static int packet_timer[GBN_BUFFER_SIZE];   /* Timer in Intervallen (dekrementierend) */
static int packet_sent[GBN_BUFFER_SIZE];    /* Flag: Paket wurde gesendet */

/* Zustand */
static int connection_active = 0;
static unsigned long current_packet_num = 0;

/* --------------------------------------------------------------- */
/*  Initialisierung / Abschluss                                   */
/* --------------------------------------------------------------- */

void initClient(char *name, const char *port)
{
    struct addrinfo hints, *res, *p;
    int status;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    
    if (name == NULL) {
        name = "::1";
    }
    
    if ((status = getaddrinfo(name, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }
    
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }
        
        /* NON-BLOCKING für asynchrones Handling */
        fcntl(sockfd, F_SETFL, O_NONBLOCK);
        
        memcpy(&server_addr, p->ai_addr, p->ai_addrlen);
        server_addr_len = p->ai_addrlen;
        break;
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        fprintf(stderr, "Failed to create socket\n");
        exit(EXIT_FAILURE);
    }
    
    base = 0;
    next_seqnum = 0;
    packet_count = 0;
    connection_active = 0;
    current_packet_num = 0;
    
    memset(send_buffer, 0, sizeof(send_buffer));
    memset(packet_timer, 0, sizeof(packet_timer));
    memset(packet_sent, 0, sizeof(packet_sent));
    
    printf("[CLIENT] Initialized (server=%s, port=%s)\n", name, port);
}

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
    
    printf("[CLIENT] SEND: type=%c seq=%lu len=%lu\n",
           req->ReqType, req->SeNr, req->FlNr);
    return 0;
}

static struct answer *receive_answer(void)
{
    static struct answer ans;
    ssize_t n;
    
    /* Non-blocking receive */
    n = recvfrom(sockfd, &ans, sizeof(ans), 0, NULL, NULL);
    
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return NULL;  /* Kein Daten verfügbar */
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

/* Alle Timer um 1 Intervall dekrementieren */
static void decrement_all_timers(void)
{
    for (int i = 0; i < packet_count; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE;
        if (packet_sent[idx] && packet_timer[idx] > 0) {
            packet_timer[idx]--;
            printf("[CLIENT] TIMER: packet[%d] (seq=%lu) = %d intervals\n",
                   idx, send_buffer[idx].SeNr, packet_timer[idx]);
        }
    }
}

/* Prüfe ob erstes Paket im Fenster einen Timeout hat */
static int has_timeout(void)
{
    if (packet_count > 0) {
        int idx = base % GBN_BUFFER_SIZE;
        if (packet_sent[idx] && packet_timer[idx] <= 0) {
            printf("[CLIENT] TIMEOUT: packet seq=%lu timer expired\n",
                   send_buffer[idx].SeNr);
            return 1;
        }
    }
    return 0;
}

/* Sende alle Pakete im Fenster erneut (Go-Back-N) */
static int retransmit_all(void)
{
    printf("[CLIENT] RETRANSMIT: Sending all packets in window (base=%lu, count=%d)\n",
           base, packet_count);
    
    for (int i = 0; i < packet_count; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE;
        if (packet_sent[idx]) {
            /* Timer zurücksetzen */
            packet_timer[idx] = GBN_TIMEOUT_UNITS;
            
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
/*  Externe ARQ-API                                                */
/* --------------------------------------------------------------- */

int arqSendHello(int winSize)
{
    struct request req;
    struct answer *ans;
    int retry_count = 0;
    const int max_retries = 10;
    struct timeval tv;
    fd_set readfds;
    
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqHello;
    req.SeNr = 0;
    req.FlNr = 0;
    
    window_size = winSize;
    printf("[CLIENT] HELLO: Sending with window=%d\n", winSize);
    
    while (retry_count < max_retries) {
        if (send_packet(&req) < 0) {
            return -1;
        }
        
        /* Mit select() auf Antwort warten */
        tv.tv_sec = (GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS) / 1000;
        tv.tv_usec = ((GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS) % 1000) * 1000;
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            ans = receive_answer();
            if (ans != NULL && (ans->AnswType == AnswHello || ans->AnswType == AnswOk)) {
                connection_active = 1;
                base = 0;
                next_seqnum = 0;
                packet_count = 0;
                current_packet_num = 0;
                printf("[CLIENT] HELLO: Acknowledged (ACK type=%c)\n", ans->AnswType);
                return 0;
            }
        } else if (ret < 0) {
            perror("select");
            return -1;
        }
        
        retry_count++;
        printf("[CLIENT] HELLO: Timeout, retry %d/%d\n", retry_count, max_retries);
    }
    
    fprintf(stderr, "[CLIENT] HELLO: No response after %d retries\n", max_retries);
    return -1;
}

int arqSendData(const struct app_unit *app, int winSize)
{
    struct request req;
    struct answer *ans;
    int interval_count = 0;
    const int max_intervals = 50;  /* Max Intervalle warten */
    struct timeval tv;              /* WICHTIG: Deklaration */
    fd_set readfds;                 /* WICHTIG: Deklaration */
    
    if (!connection_active) {
        fprintf(stderr, "[CLIENT] ERROR: No active connection\n");
        return -1;
    }
    
    if (!app || app->len <= 0) {
        fprintf(stderr, "[CLIENT] ERROR: Invalid app unit\n");
        return -1;
    }
    
    /* Request vorbereiten */
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
    /* HAUPTSCHLEIFE: Pro Intervall - max 1 neues Paket oder Retransmit */
    /* ============================================================ */
    
    int packet_in_buffer = 0;  /* Flag: Paket wurde in Puffer eingefügt */
    
    while (interval_count < max_intervals) {
        printf("[CLIENT] --- INTERVAL %d START (window=%d/%d, base=%lu, next=%lu) ---\n",
               interval_count, packet_count, winSize, base, next_seqnum);
        
        /* 1. Prüfe ob Timeout vorliegt */
        if (has_timeout()) {
            printf("[CLIENT] TIMEOUT at interval %d\n", interval_count);
            if (retransmit_all() < 0) {
                return -1;
            }
        }
        
        /* 2. Versuche neues Paket ins Fenster zu schreiben (falls noch nicht drin) */
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
        
        /* 3. Sende ein Paket (neues oder Retransmit) */
        if (packet_in_buffer && packet_count > 0) {
            int idx;
            if (packet_in_buffer && send_buffer[req.SeNr % GBN_BUFFER_SIZE].SeNr == req.SeNr) {
                /* Sende gerade eingefügtes Paket */
                idx = req.SeNr % GBN_BUFFER_SIZE;
            } else {
                /* Sende erstes Paket im Fenster (bei Retransmit) */
                idx = base % GBN_BUFFER_SIZE;
            }
            
            if (send_packet(&send_buffer[idx]) < 0) {
                return -1;
            }
        }
        
        /* 4. Warte genau ein Intervall (GBN_TIMEOUT_INT_MS) auf ACK oder Timeout */
        tv.tv_sec = GBN_TIMEOUT_INT_MS / 1000;
        tv.tv_usec = (GBN_TIMEOUT_INT_MS % 1000) * 1000;
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            /* ACK empfangen */
            ans = receive_answer();
            if (ans != NULL && ans->AnswType == AnswOk) {
                unsigned long ack_seq = ans->SeNo;
                
                printf("[CLIENT] ACK RECEIVED: ack_seq=%lu\n", ack_seq);
                
                /* Prüfe ob ACK im Fenster liegt */
                if (ack_seq >= base && ack_seq <= next_seqnum) {
                    if (ack_seq > base) {
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
                    
                    /* Wurde unser Paket bestätigt? */
                    if (ack_seq > req.SeNr) {
                        printf("[CLIENT] CONFIRMED: packet seq=%lu confirmed\n", req.SeNr);
                        current_packet_num++;
                        return 0;  /* SUCCESS */
                    }
                } else {
                    printf("[CLIENT] ACK OUT OF WINDOW: %lu not in [%lu, %lu) - ignored\n",
                           ack_seq, base, next_seqnum);
                }
            }
        } else if (ret < 0) {
            perror("select");
            return -1;
        }
        /* ret == 0: select timeout - Intervall vorbei */
        
        /* 5. Dekrementiere alle Timer */
        decrement_all_timers();
        
        printf("[CLIENT] --- INTERVAL %d END ---\n\n", interval_count);
        
        interval_count++;
    }
    
    fprintf(stderr, "[CLIENT] ERROR: Max intervals exceeded for packet %lu\n", 
            current_packet_num);
    return -3;
}

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
    
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqClose;
    req.SeNr = current_packet_num;
    req.FlNr = 0;
    
    printf("[CLIENT] CLOSE: Sending close (seq=%lu)\n", current_packet_num);
    
    while (retry_count < max_retries) {
        if (send_packet(&req) < 0) {
            return -1;
        }
        
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
                return 0;
            }
        } else if (ret < 0) {
            perror("select");
            return -1;
        }
        
        retry_count++;
        printf("[CLIENT] CLOSE: Timeout, retry %d/%d\n", retry_count, max_retries);
    }
    
    fprintf(stderr, "[CLIENT] CLOSE: No response after %d retries\n", max_retries);
    return -1;
}