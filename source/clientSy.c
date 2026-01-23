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
static long packet_timers[GBN_BUFFER_SIZE];  /* Timer in Millisekunden */
static int packet_sent[GBN_BUFFER_SIZE];     /* Paket wurde gesendet */
static time_t packet_send_time[GBN_BUFFER_SIZE]; /* Zeitstempel des Sendens */

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
    memset(packet_send_time, 0, sizeof(packet_send_time));
    
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

/* Timer aktualisieren und Timeouts prüfen */
static int check_and_handle_timeouts(int winSize)
{
    time_t now = time(NULL);
    int retransmitted = 0;
    
    for (int i = 0; i < packet_count; i++) {
        int idx = (base + i) % GBN_BUFFER_SIZE;
        
        if (packet_sent[idx] && packet_send_time[idx] > 0) {
            double elapsed = difftime(now, packet_send_time[idx]);
            
            /* Timeout prüfen: GBN_TIMEOUT_UNITS * GBN_TIMEOUT_INT_MS Millisekunden */
            double timeout_seconds = (GBN_TIMEOUT_UNITS * GBN_TIMEOUT_INT_MS) / 1000.0;
            
            if (elapsed >= timeout_seconds) {
                printf("Client: Timeout detected for packet %lu (elapsed=%.1fs)\n", 
                       send_buffer[idx].SeNr, elapsed);
                retransmitted = 1;
            }
        }
    }
    
    /* Alle Pakete im Fenster erneut senden, falls Timeout */
    if (retransmitted) {
        printf("Client: Retransmitting window (base=%lu, count=%d)\n", base, packet_count);
        for (int i = 0; i < winSize && i < packet_count; i++) {
            int idx = (base + i) % GBN_BUFFER_SIZE;
            if (packet_sent[idx]) {
                packet_send_time[idx] = now;
                if (send_packet(&send_buffer[idx]) < 0) {
                    fprintf(stderr, "Client: Retransmission failed for packet %lu\n",
                            send_buffer[idx].SeNr);
                    return -1;
                } else {
                    printf("Client: Retransmitted packet %lu\n", send_buffer[idx].SeNr);
                }
            }
        }
        return 1;  /* Retransmission durchgeführt */
    }
    
    return 0;  /* Kein Timeout */
}

/* --------------------------------------------------------------- */
/*  Verbindungs-Setup und -Abbau (Hello, Close)                   */
/* --------------------------------------------------------------- */

int arqSendHello(int winSize) 
{
    struct request req;
    struct answer *ans;
    int retry_count = 0;
    int max_retries = 5;
    
    /* Request vorbereiten */
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqHello;
    req.SeNr = 0;
    req.FlNr = 0;
    
    /* Fenstergröße speichern */
    window_size = winSize;
    
    printf("Client: Sending Hello (window=%d)\n", winSize);
    
    /* Hello mit Retry senden */
    while (retry_count < max_retries) {
        if (send_packet(&req) < 0) {
            fprintf(stderr, "Client: Failed to send Hello\n");
            return -1;
        }
        
        /* Auf Antwort warten */
        ans = receive_answer(GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS);
        
        if (ans != NULL) {
            if (ans->AnswType == AnswHello || ans->AnswType == AnswOk) {
                connection_active = 1;
                base = 0;
                next_seqnum = 0;
                packet_count = 0;
                printf("Client: Hello acknowledged\n");
                return 0;
            } else {
                fprintf(stderr, "Client: Invalid response to Hello: %c\n", ans->AnswType);
                return -2;
            }
        }
        
        retry_count++;
        printf("Client: Hello timeout, retry %d/%d\n", retry_count, max_retries);
        usleep(GBN_TIMEOUT_INT_MS * 1000);
    }
    
    fprintf(stderr, "Client: No response to Hello after %d retries\n", max_retries);
    return -1;
}

int arqSendClose(int winSize) 
{
    struct request req;
    struct answer *ans;
    int retry_count = 0;
    int max_retries = 5;
    
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
    
    /* Close mit Retry senden */
    while (retry_count < max_retries) {
        if (send_packet(&req) < 0) {
            fprintf(stderr, "Client: Failed to send Close\n");
            return -1;
        }
        
        ans = receive_answer(GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS);
        
        if (ans != NULL) {
            if (ans->AnswType == AnswOk) {
                connection_active = 0;
                printf("Client: Close acknowledged\n");
                return 0;
            } else {
                fprintf(stderr, "Client: Invalid response to Close: %c\n", ans->AnswType);
                return -2;
            }
        }
        
        retry_count++;
        printf("Client: Close timeout, retry %d/%d\n", retry_count, max_retries);
        usleep(GBN_TIMEOUT_INT_MS * 1000);
    }
    
    fprintf(stderr, "Client: No response to Close after %d retries\n", max_retries);
    return -1;
}

/* --------------------------------------------------------------- */
/*  Datenübertragung mit Go-Back-N ARQ                            */
/* --------------------------------------------------------------- */

int arqSendData(const struct app_unit *app, int winSize) 
{
    struct request req;
    struct answer *ans;
    int idx;
    int retry_count = 0;
    int max_retries = 10;
    
    if (!connection_active) {
        fprintf(stderr, "Client: No active connection\n");
        return -1;
    }
    
    if (!app || app->len <= 0) {
        fprintf(stderr, "Client: Invalid app unit\n");
        return -1;
    }
    
    /* Neue Request vorbereiten */
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqData;
    req.SeNr = current_packet_num;  /* Sequenznummer für dieses Paket */
    
    /* Daten kopieren (begrenzt auf BufferSize) */
    size_t copy_len = app->len;
    if (copy_len >= BufferSize) {
        copy_len = BufferSize - 1;
        fprintf(stderr, "Client: Data truncated to %zu bytes\n", copy_len);
    }
    
    memcpy(req.name, app->data, copy_len);
    req.name[copy_len] = '\0';
    req.FlNr = copy_len;
    
    printf("Client: Preparing Data packet %lu (len=%lu)\n", 
           current_packet_num, req.FlNr);
    
    /* ============================================================ */
    /* HAUPTSCHLEIFE: Paket senden und auf ACK warten               */
    /* ============================================================ */
    
    while (retry_count < max_retries) {
        /* Fenster voll? */
        if (packet_count >= winSize) {
            printf("Client: Window full (%d/%d), waiting for ACK...\n", 
                   packet_count, winSize);
            
            /* Auf ACK warten, um Fenster zu leeren */
            ans = receive_answer(GBN_TIMEOUT_INT_MS);
            if (ans != NULL) {
                /* ACK verarbeiten */
                if (ans->AnswType == AnswOk) {
                    unsigned long ack_seq = ans->SeNo;
                    
                    if (ack_seq > base) {
                        int packets_to_free = ack_seq - base;
                        
                        printf("Client: Window advanced (%lu -> %lu), freed %d packets\n",
                               base, ack_seq, packets_to_free);
                        
                        /* Alte Pakete aus Puffer entfernen */
                        for (int i = 0; i < packets_to_free && i < packet_count; i++) {
                            int free_idx = (base + i) % GBN_BUFFER_SIZE;
                            packet_sent[free_idx] = 0;
                            packet_send_time[free_idx] = 0;
                        }
                        
                        packet_count -= packets_to_free;
                        if (packet_count < 0) packet_count = 0;
                        base = ack_seq;
                    }
                } else if (ans->AnswType == AnswWarn || ans->AnswType == AnswErr) {
                    fprintf(stderr, "Client: Received error response: %lu\n", ans->ErrNo);
                    /* Fenster zurücksetzen und von vorne senden */
                    break;
                }
            }
            
            /* Timeouts prüfen */
            if (check_and_handle_timeouts(winSize) < 0) {
                return -1;
            }
            
            continue;
        }
        
        /* Paket im Puffer speichern (nur beim ersten Mal) */
        if (retry_count == 0) {
            idx = req.SeNr % GBN_BUFFER_SIZE;
            memcpy(&send_buffer[idx], &req, sizeof(struct request));
            packet_sent[idx] = 1;
            packet_send_time[idx] = time(NULL);
            packet_count++;
            next_seqnum++;  /* Nächste Paketnum für nächsten Aufruf */
            
            printf("Client: Stored packet %lu in buffer[%d] (window=%d/%d)\n",
                   req.SeNr, idx, packet_count, winSize);
        }
        
        /* Paket senden */
        if (send_packet(&req) < 0) {
            fprintf(stderr, "Client: Failed to send packet %lu\n", current_packet_num);
            return -1;
        }
        
        /* Auf ACK warten */
        ans = receive_answer(GBN_TIMEOUT_INT_MS * GBN_TIMEOUT_UNITS);
        
        if (ans != NULL) {
            /* Antwort erhalten */
            if (ans->AnswType == AnswOk) {
                unsigned long ack_seq = ans->SeNo;
                
                /* Fenster verschieben basierend auf kumulativem ACK */
                if (ack_seq > base) {
                    int packets_to_free = ack_seq - base;
                    
                    printf("Client: Received ACK for seq %lu (base: %lu -> %lu)\n",
                           ack_seq, base, ack_seq);
                    
                    for (int i = 0; i < packets_to_free && i < packet_count; i++) {
                        int free_idx = (base + i) % GBN_BUFFER_SIZE;
                        packet_sent[free_idx] = 0;
                        packet_send_time[free_idx] = 0;
                    }
                    
                    packet_count -= packets_to_free;
                    if (packet_count < 0) packet_count = 0;
                    base = ack_seq;
                }
                
                /* Erfolg! */
                current_packet_num++;
                printf("Client: Packet %lu successfully transmitted\n", 
                       current_packet_num - 1);
                return 0;
                
            } else if (ans->AnswType == AnswWarn || ans->AnswType == AnswErr) {
                fprintf(stderr, "Client: Error/Warning for packet %lu: %lu\n",
                        current_packet_num, ans->ErrNo);
                return -4;
            }
        }
        
        /* ACK-Timeout: Timeouts prüfen und ggf. retransmit */
        int timeout_result = check_and_handle_timeouts(winSize);
        if (timeout_result < 0) {
            return -1;  /* Fehler beim Retransmit */
        }
        
        retry_count++;
        printf("Client: ACK timeout for packet %lu, retry %d/%d\n",
               current_packet_num, retry_count, max_retries);
        usleep(GBN_TIMEOUT_INT_MS * 500);  /* Kurz warten vor nächstem Versuch */
    }
    
    fprintf(stderr, "Client: Max retries exceeded for packet %lu\n", current_packet_num);
    return -3;
}