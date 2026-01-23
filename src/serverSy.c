#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

/* Globale Variablen */
static int sockfd = -1;
static struct sockaddr_storage client_addr;
static socklen_t client_addr_len = sizeof(client_addr);
static unsigned long nextExpected = 0;
static appStartFn appStartCallback = NULL;
static appWriteFn appWriteCallback = NULL;
static appEndFn appEndCallback = NULL;
static int transferActive = 0;

/* --------------------------------------------------------------- */
/*  SAP-Schicht (UDP)                                              */
/* --------------------------------------------------------------- */

int initServer(const char *port)
{
    struct addrinfo hints, *res, *p;
    int status;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    
    if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }
    
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }
        
        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("bind");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        
        break;
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        fprintf(stderr, "Failed to bind socket\n");
        return -1;
    }
    
    printf("[SERVER] Socket bound to port %s\n", port);
    return 0;
}

struct request *getRequest(void)
{
    static struct request req;
    ssize_t n;
    
    client_addr_len = sizeof(client_addr);
    n = recvfrom(sockfd, &req, sizeof(req), 0,
                 (struct sockaddr *)&client_addr, &client_addr_len);
    
    if (n == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return NULL;
        }
        perror("recvfrom");
        return NULL;
    }
    
    if (n < (ssize_t)sizeof(req.ReqType)) {
        fprintf(stderr, "[SERVER] Packet too short\n");
        return NULL;
    }
    
    printf("[SERVER] RECV: type=%c seq=%lu len=%lu\n",
           req.ReqType, req.SeNr, req.FlNr);
    return &req;
}

int sendAnswer(struct answer *answerPtr)
{
    ssize_t n;
    
    if (sockfd == -1) return -1;
    
    n = sendto(sockfd, answerPtr, sizeof(*answerPtr), 0,
               (struct sockaddr *)&client_addr, client_addr_len);
    
    if (n == -1) {
        perror("sendto");
        return -1;
    }
    
    printf("[SERVER] SEND: type=%c seq/err=%lu\n",
           answerPtr->AnswType, answerPtr->SeNo);
    return 0;
}

int exitServer(void)
{
    if (sockfd != -1) {
        close(sockfd);
        sockfd = -1;
    }
    
    nextExpected = 0;
    transferActive = 0;
    return 0;
}

/* --------------------------------------------------------------- */
/*  ARQ-/GBN-Logik (Empfänger)                                     */
/* --------------------------------------------------------------- */

static struct answer *processRequest(struct request *reqPtr,
                                     struct answer *answPtr,
                                     double lossReq)
{
    static struct answer answ;
    
    /* Paketverlust simulieren */
    if (lossReq > 0.0) {
        double r = (double)rand() / RAND_MAX;
        if (r < lossReq) {
            printf("[SERVER] LOSS: Packet loss simulated (seq=%lu)\n", reqPtr->SeNr);
            return NULL;  /* Paket wird verworfen */
        }
    }
    
    memset(&answ, 0, sizeof(answ));
    answ.FlNr = 0;
    
    switch (reqPtr->ReqType) {
        case ReqHello:
            printf("[SERVER] HELLO received\n");
            nextExpected = 0;
            transferActive = 0;
            
            if (appStartCallback != NULL) {
                if (appStartCallback() != 0) {
                    answ.AnswType = AnswErr;
                    answ.ErrNo = ERR_FILE_ERROR;
                    fprintf(stderr, "[SERVER] File open failed\n");
                    break;
                }
                transferActive = 1;
            }
            
            answ.AnswType = AnswHello;
            answ.SeNo = 0;
            printf("[SERVER] HELLO accepted, nextExpected=0\n");
            break;
            
        case ReqData:
            if (!transferActive) {
                answ.AnswType = AnswErr;
                answ.ErrNo = ERR_ILLEGAL_REQUEST;
                fprintf(stderr, "[SERVER] DATA without active transfer\n");
                break;
            }
            
            if (reqPtr->SeNr == nextExpected) {
                /* In-Order Packet */
                printf("[SERVER] DATA seq=%lu INORDER\n", reqPtr->SeNr);
                
                if (appWriteCallback != NULL) {
                    if (appWriteCallback(reqPtr->name, reqPtr->FlNr) != 0) {
                        answ.AnswType = AnswErr;
                        answ.ErrNo = ERR_FILE_ERROR;
                        fprintf(stderr, "[SERVER] Write failed\n");
                        break;
                    }
                }
                
                nextExpected++;
                answ.AnswType = AnswOk;
                answ.SeNo = nextExpected;  /* Kumulative ACK */
                printf("[SERVER] ACK: next expected = %lu\n", nextExpected);
                
            } else if (reqPtr->SeNr < nextExpected) {
                /* Duplicate - already processed */
                answ.AnswType = AnswOk;
                answ.SeNo = nextExpected;
                printf("[SERVER] DATA seq=%lu DUPLICATE, sending ACK %lu\n",
                       reqPtr->SeNr, nextExpected);
                
            } else {
                /* Out-of-order - drop */
                answ.AnswType = AnswWarn;
                answ.ErrNo = ERR_WRONG_SEQ;
                printf("[SERVER] DATA seq=%lu OUT-OF-ORDER (expected %lu) - DROPPED\n",
                       reqPtr->SeNr, nextExpected);
            }
            break;
            
        case ReqClose:
            printf("[SERVER] CLOSE received\n");
            if (transferActive && appEndCallback != NULL) {
                appEndCallback();
                transferActive = 0;
            }
            answ.AnswType = AnswOk;
            answ.SeNo = nextExpected;
            printf("[SERVER] CLOSE accepted\n");
            break;
            
        default:
            answ.AnswType = AnswErr;
            answ.ErrNo = ERR_ILLEGAL_REQUEST;
            fprintf(stderr, "[SERVER] Unknown request type: %c\n", reqPtr->ReqType);
            break;
    }
    
    if (answPtr != NULL) {
        *answPtr = answ;
    }
    
    return &answ;
}

/* --------------------------------------------------------------- */
/*  ARQ-Server-Hauptschleife                                       */
/* --------------------------------------------------------------- */

int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd)
{
    struct request *req;
    struct answer answ;
    
    appStartCallback = appStart;
    appWriteCallback = appWrite;
    appEndCallback = appEnd;
    
    srand(time(NULL));
    
    if (initServer(port) != 0) {
        return -1;
    }
    
    printf("[SERVER] Main loop started\n");
    while (1) {
        /* Warte auf Request - non-blocking mit kurzen Intervallen */
        struct timeval tv;
        fd_set readfds;
        
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  /* 100ms */
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret < 0) {
            perror("select");
            break;
        }
        
        if (ret == 0) {
            /* Timeout - keine Daten verfügbar */
            continue;
        }
        
        /* Daten verfügbar */
        req = getRequest();
        if (req == NULL) {
            continue;
        }
        
        /* Request verarbeiten */
        struct answer *answPtr = processRequest(req, &answ, lossReq);
        if (answPtr == NULL) {
            /* Request wurde verworfen (Paketverlust) */
            continue;
        }
        
        /* ACK-Verlust simulieren */
        if (lossAck > 0.0) {
            double r = (double)rand() / RAND_MAX;
            if (r < lossAck) {
                printf("[SERVER] LOSS: ACK loss simulated (type=%c)\n", answ.AnswType);
                continue;  /* ACK wird nicht gesendet */
            }
        }
        
        /* ACK senden */
        if (sendAnswer(&answ) != 0) {
            fprintf(stderr, "[SERVER] Error sending answer\n");
        }
        
        /* Close-Handshake beendet? */
        if (req->ReqType == ReqClose && answ.AnswType == AnswOk) {
            printf("[SERVER] Transfer complete, exiting\n");
            break;
        }
    }
    
    exitServer();
    return 0;
}