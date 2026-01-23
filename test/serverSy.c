/* serverSy.c - UDP + ARQ/Go-Back-N-Server mit Loss-Simulation
 *
 * Schichten:
 *   - SAP-Schicht (UDP): initServer, getRequest, sendAnswer, exitServer
 *   - ARQ-Schicht: processRequest(), arqServerLoop()
 *
 * Die Anwendung (Datei öffnen/schreiben/schließen) wird über Callbacks
 * aus server.c angebunden:
 *   appStartFn  appStartTransfer
 *   appWriteFn  appWriteData
 *   appEndFn    appEndTransfer
 *
 * WICHTIG:
 *   - Dateiname und Funktionssignaturen in serverSy.h sollen
 *     unverändert beibehalten werden.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

/* Globale Variablen */
static int g_sockfd = -1;
static struct sockaddr_storage g_client_addr;
static socklen_t g_client_addr_len = sizeof(g_client_addr);
static unsigned long g_next_expected = 0;
static int g_connection_active = 0;

/* Callback-Funktionen */
static appStartFn g_appStart = NULL;
static appWriteFn g_appWrite = NULL;
static appEndFn g_appEnd = NULL;

/* --------------------------------------------------------------- */
/*  SAP-Schicht (UDP)                                              */
/* --------------------------------------------------------------- */

int initServer(const char *port)
{
    struct addrinfo hints, *res, *p;
    int ret;
    
    /* Hints für getaddrinfo setzen */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;       /* IPv6 */
    hints.ai_socktype = SOCK_DGRAM;   /* UDP */
    hints.ai_flags = AI_PASSIVE;      /* Für Server, eigene Adresse verwenden */
    
    /* Adresse auflösen */
    ret = getaddrinfo(NULL, port, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "Server: getaddrinfo failed: %s\n", gai_strerror(ret));
        return -1;
    }
    
    /* Passende Adresse finden und Socket erstellen */
    for (p = res; p != NULL; p = p->ai_next) {
        g_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (g_sockfd < 0) {
            continue; /* Nächste Adresse versuchen */
        }
        
        /* Socket an Adresse binden */
        if (bind(g_sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break; /* Erfolg */
        }
        
        close(g_sockfd);
        g_sockfd = -1;
    }
    
    freeaddrinfo(res);
    
    if (g_sockfd < 0) {
        fprintf(stderr, "Server: Failed to create and bind socket\n");
        return -1;
    }
    
    printf("Server: Listening on port %s\n", port);
    return 0;
}

struct request *getRequest(void)
{
    static struct request req;
    ssize_t numBytes;
    
    /* Paket vom Client empfangen */
    numBytes = recvfrom(g_sockfd, &req, sizeof(req), 0,
                       (struct sockaddr *)&g_client_addr, &g_client_addr_len);
    
    if (numBytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return NULL;
    }
    
    if (numBytes == 0) {
        return NULL;
    }
    
    /* Empfangene Bytes validieren */
    if (numBytes < (ssize_t)sizeof(req.ReqType) + sizeof(req.FlNr) + sizeof(req.SeNr)) {
        fprintf(stderr, "Server: Received incomplete packet (%ld bytes)\n", (long)numBytes);
        return NULL;
    }
    
    return &req;
}

int sendAnswer(struct answer *answerPtr)
{
    ssize_t numBytes;
    
    if (!answerPtr || g_sockfd < 0) {
        return -1;
    }
    
    /* Antwort an Client senden */
    numBytes = sendto(g_sockfd, answerPtr, sizeof(*answerPtr), 0,
                     (struct sockaddr *)&g_client_addr, g_client_addr_len);
    
    if (numBytes < 0) {
        perror("sendto");
        return -1;
    }
    
    return 0;
}

int exitServer(void)
{
    if (g_sockfd >= 0) {
        close(g_sockfd);
        g_sockfd = -1;
    }
    
    g_connection_active = 0;
    g_next_expected = 0;
    
    return 0;
}

/* --------------------------------------------------------------- */
/*  ARQ-/GBN-Logik (Empfänger)                                     */
/* --------------------------------------------------------------- */

static struct answer *processRequest(struct request *reqPtr,
                                     struct answer *answPtr,
                                     double lossReq)
{
    /* Paketverlust simulieren */
    if (lossReq > 0.0) {
        double randVal = (double)rand() / RAND_MAX;
        if (randVal < lossReq) {
            printf("Server: Simulating request loss (seq=%lu)\n", reqPtr->SeNr);
            return NULL; /* Paket verworfen */
        }
    }
    
    /* Antwort initialisieren */
    memset(answPtr, 0, sizeof(*answPtr));
    
    switch (reqPtr->ReqType) {
    case ReqHello:
        printf("Server: Received Hello (seq=%lu)\n", reqPtr->SeNr);
        
        /* Verbindung initialisieren */
        g_next_expected = 0;
        g_connection_active = 1;
        
        /* Anwendung informieren */
        if (g_appStart != NULL) {
            if (g_appStart() < 0) {
                answPtr->AnswType = AnswErr;
                answPtr->ErrNo = ERR_FILE_ERROR;
                return answPtr;
            }
        }
        
        /* Hello bestätigen */
        answPtr->AnswType = AnswHello;
        answPtr->SeNo = 0;
        break;
        
    case ReqData:
        printf("Server: Received Data (seq=%lu, expected=%lu)\n", 
               reqPtr->SeNr, g_next_expected);
        
        /* Sequenznummer prüfen */
        if (reqPtr->SeNr == g_next_expected) {
            /* In-Order Paket: Anwendung informieren */
            if (g_appWrite != NULL) {
                /* Datenlänge begrenzen */
                unsigned long data_len = reqPtr->FlNr;
                if (data_len > BufferSize) {
                    data_len = BufferSize;
                }
                
                if (g_appWrite(reqPtr->name, data_len) < 0) {
                    answPtr->AnswType = AnswErr;
                    answPtr->ErrNo = ERR_FILE_ERROR;
                    return answPtr;
                }
            }
            
            g_next_expected++;
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = g_next_expected; /* Nächste erwartete SeqNr */
        } else {
            /* Out-of-Order Paket: verwerfen */
            printf("Server: Out-of-order packet (seq=%lu, expected=%lu) - dropping\n",
                   reqPtr->SeNr, g_next_expected);
            answPtr->AnswType = AnswWarn;
            answPtr->ErrNo = ERR_WRONG_SEQ;
        }
        break;
        
    case ReqClose:
        printf("Server: Received Close (seq=%lu)\n", reqPtr->SeNr);
        
        /* Anwendung informieren */
        if (g_appEnd != NULL) {
            g_appEnd();
        }
        
        g_connection_active = 0;
        answPtr->AnswType = AnswOk;
        answPtr->SeNo = reqPtr->SeNr + 1; /* Close bestätigen */
        break;
        
    default:
        fprintf(stderr, "Server: Illegal request type: %c\n", reqPtr->ReqType);
        answPtr->AnswType = AnswErr;
        answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
        break;
    }
    
    return answPtr;
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
    /* Callbacks speichern */
    g_appStart = appStart;
    g_appWrite = appWrite;
    g_appEnd = appEnd;
    
    /* Server initialisieren */
    if (initServer(port) < 0) {
        return -1;
    }
    
    /* Zufallsgenerator für Verlustsimulation */
    srand((unsigned int)time(NULL));
    
    printf("Server: ARQ loop started (lossReq=%.2f, lossAck=%.2f)\n", lossReq, lossAck);
    
    /* Hauptschleife */
    while (1) {
        struct request *reqPtr;
        struct answer ans;
        struct answer *ansPtr;
        
        /* Request empfangen */
        reqPtr = getRequest();
        if (reqPtr == NULL) {
            if (!g_connection_active) {
                /* Keine aktive Verbindung, kurz warten */
                usleep(10000); /* 10ms */
            }
            continue;
        }
        
        /* Request verarbeiten */
        ansPtr = processRequest(reqPtr, &ans, lossReq);
        if (ansPtr == NULL) {
            /* Request wurde verworfen (Paketverlust) */
            continue;
        }
        
        /* ACK-Verlust simulieren */
        if (lossAck > 0.0) {
            double randVal = (double)rand() / RAND_MAX;
            if (randVal < lossAck) {
                printf("Server: Simulating ACK loss (type=%c, seq=%lu)\n",
                       ansPtr->AnswType, ansPtr->SeNo);
                continue; /* ACK verwerfen */
            }
        }
        
        /* Antwort senden */
        if (sendAnswer(ansPtr) < 0) {
            fprintf(stderr, "Server: Failed to send answer\n");
            /* Weiter versuchen */
        } else {
            printf("Server: Sent answer (type=%c, seq/err=%lu)\n",
                   ansPtr->AnswType, ansPtr->SeNo);
        }
        
        /* Bei Close: Schleife beenden */
        if (reqPtr->ReqType == ReqClose) {
            printf("Server: Connection closed by client\n");
            break;
        }
    }
    
    /* Server beenden */
    exitServer();
    return 0;
}
