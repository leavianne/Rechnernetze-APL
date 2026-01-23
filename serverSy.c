#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <arpa/inet.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

// Globale Variablen
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
    
    printf("Server: Socket gebunden an Port %s\n", port);
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
        fprintf(stderr, "Server: Zu kurzes Paket empfangen\n");
        return NULL;
    }
    
    printf("Server: Empfangen - Typ:%c Seq:%lu Len:%lu\n", 
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
    
    printf("Server: Gesendet - Typ:%c Seq/Err:%lu\n", 
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
    
    // Paketverlust simulieren
    if (lossReq > 0.0) {
        double r = (double)rand() / RAND_MAX;
        if (r < lossReq) {
            printf("Server: Paketverlust simuliert (Seq:%lu)\n", reqPtr->SeNr);
            return NULL;
        }
    }
    
    // Antwort vorbereiten
    memset(&answ, 0, sizeof(answ));
    answ.FlNr = 0;
    
    switch (reqPtr->ReqType) {
        case ReqHello:
            printf("Server: Hello empfangen\n");
            nextExpected = 0;
            transferActive = 0;
            
            if (appStartCallback != NULL) {
                if (appStartCallback() != 0) {
                    answ.AnswType = AnswErr;
                    answ.ErrNo = ERR_FILE_ERROR;
                    break;
                }
                transferActive = 1;
            }
            
            answ.AnswType = AnswHello;
            answ.SeNo = 0;
            break;
            
        case ReqData:
            if (!transferActive) {
                answ.AnswType = AnswErr;
                answ.ErrNo = ERR_ILLEGAL_REQUEST;
                break;
            }
            
            if (reqPtr->SeNr == nextExpected) {
                // Korrekte Sequenznummer
                if (appWriteCallback != NULL) {
                    if (appWriteCallback(reqPtr->name, reqPtr->FlNr) != 0) {
                        answ.AnswType = AnswErr;
                        answ.ErrNo = ERR_FILE_ERROR;
                        break;
                    }
                }
                
                nextExpected++;
                answ.AnswType = AnswOk;
                answ.SeNo = nextExpected;  // Nächstes erwartete SeqNr
                printf("Server: Paket %lu akzeptiert, nextExpected=%lu\n", 
                       reqPtr->SeNr, nextExpected);
            } else if (reqPtr->SeNr < nextExpected) {
                // Duplikat - bereits empfangen
                answ.AnswType = AnswOk;
                answ.SeNo = nextExpected;
                printf("Server: Duplikat von Paket %lu ignoriert\n", reqPtr->SeNr);
            } else {
                // Out-of-order - verwerfen
                answ.AnswType = AnswWarn;
                answ.ErrNo = ERR_WRONG_SEQ;
                printf("Server: Out-of-order Paket %lu erwartet %lu\n", 
                       reqPtr->SeNr, nextExpected);
            }
            break;
            
        case ReqClose:
            printf("Server: Close empfangen\n");
            if (transferActive && appEndCallback != NULL) {
                appEndCallback();
                transferActive = 0;
            }
            answ.AnswType = AnswOk;
            answ.SeNo = nextExpected;
            break;
            
        default:
            answ.AnswType = AnswErr;
            answ.ErrNo = ERR_ILLEGAL_REQUEST;
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

    // Callbacks speichern
    appStartCallback = appStart;
    appWriteCallback = appWrite;
    appEndCallback = appEnd;

    // Zufall für Verlustsimulation initialisieren
    srand(time(NULL));

    // Server initialisieren
    if (initServer(port) != 0) {
        return -1;
    }

    // Hauptschleife
    printf("Server: Starte Hauptschleife\n");
    while (1) {
        req = getRequest();
        if (req == NULL) {
            usleep(10000);  // Kurz warten, um CPU-Last zu reduzieren
            continue;
        }
        
        struct answer *answPtr = processRequest(req, &answ, lossReq);
        if (answPtr == NULL) {
            continue;  // Paket wurde verworfen (Verlust)
        }

        // ACK-Verlust simulieren
        if (lossAck > 0.0) {
            double r = (double)rand() / RAND_MAX;
            if (r < lossAck) {
                printf("Server: ACK-Verlust simuliert (Typ:%c)\n", answ.AnswType);
                continue;
            }
        }

        // Antwort senden
        if (sendAnswer(&answ) != 0) {
            fprintf(stderr, "Server: Fehler beim Senden der Antwort\n");
        }

        // Bei Close und OK: Server beenden
        if (req->ReqType == ReqClose && answ.AnswType == AnswOk) {
            printf("Server: Transfer abgeschlossen, beende...\n");
            break;
        }
    }
    
    exitServer();
    return 0;
}
