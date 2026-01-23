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

/* --------------------------------------------------------------- */
/*  Globale Variablen - ARQ-Empfänger Zustand                      */
/* --------------------------------------------------------------- */

static int sockfd = -1;                                  // UDP Socket-Deskriptor         
static struct sockaddr_storage client_addr;              // Speichert die Client-Adresse
static socklen_t client_addr_len = sizeof(client_addr);     

// ARQ Zustandsvariablen
static unsigned long nextExpected = 0;                   // Nächste erwartete Sequenznummer    

// Anwendungs-Callbacks
static appStartFn appStartCallback = NULL;               // Callback: Transfer startet
static appWriteFn appWriteCallback = NULL;               // Callback: Daten schreiben
static appEndFn appEndCallback = NULL;                   // Callback: Transfer endet

// Transfer-Status
static int transferActive = 0;                           // Flag: ist ein Transfer aktiv?

/* --------------------------------------------------------------- */
/*  SAP-Schicht (UDP) - Socket-Verwaltung                          */
/* --------------------------------------------------------------- */

/**
 * initServer() - Initialisiert den UDP Server Socket
 * 
 * Aufgaben:
 * - Socket (IPv6, UDP) erstellen
 * - An Port binden
 * - Wartet auf eingehende Pakete
 * 
 * Parameter:
 * - port: Portnummer als String (z.B. "3333")
 * 
 * Rückgabewert:
 * 0: Erfolg
 * <0: Fehler
 */
int initServer(const char *port)
{
    struct addrinfo hints, *res, *p;
    int status;
    
    // Hinweise für getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;     // IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP
    hints.ai_flags = AI_PASSIVE;    // Verwende NULL als Hostname -> alle Interfaces

    // DNS-Auflösung: NULL + port -> addrinfo struktur
    if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }
    
    // Versuche Socket mit jeder der zurückgegebenen Adressen zu erstellen
    for (p = res; p != NULL; p = p->ai_next) {
        // Socket erstellen
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }

        /**
         * Socket-Option: SO_REUSEADDR
         * Erlaubt, den Port sofort nach Beenden wiederzuverwenden
         * (ohne 2-Minuten-Wartezeit)
         */
        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        
        // Bind: Socket an Adresse + Port binden
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("bind");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        
        break;  // Erfolg! Socket ist erstellt und gebunden
    }
    
    // Speicher freigeben
    freeaddrinfo(res);
    
    if (p == NULL) {
        fprintf(stderr, "Failed to bind socket\n");
        return -1;
    }
    
    printf("[SERVER] Socket bound to port %s\n", port);
    return 0;   // Erfolg
}

/**
 * getRequest() - Empfängt ein Request-Paket vom Client
 * 
 * Benutzt: recvfrom() (blocking)
 * Speichert die Client-Adresse für Antworten
 * 
 * Rückgabewert:
 * Zeiger auf struct request
 * NULL: Fehler oder kein Paket
 */
struct request *getRequest(void)
{
    static struct request req;  // Statisch: Speichert das letzte empfangene Paket
    ssize_t n;
    
    // Recvfrom: UDP-Paket empfangen
    client_addr_len = sizeof(client_addr);
    n = recvfrom(sockfd,                            // Socket
                &req,                               // Buffer
                sizeof(req),                        // Buffergröße   
                0,                                  // Flags (0 = blocking)
                (struct sockaddr *)&client_addr,    // Speichert Client-Adresse
                &client_addr_len);                  // Länge der Adresse
    
    if (n == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return NULL;    // Kein Paket verfügbar
        }
        perror("recvfrom");
        return NULL;
    }
    
    // Prüfe ob Paket groß genug ist
    if (n < (ssize_t)sizeof(req.ReqType)) {
        fprintf(stderr, "[SERVER] Packet too short\n");
        return NULL;
    }
    
    // Debug-Ausgabe
    printf("[SERVER] RECV: type=%c seq=%lu len=%lu\n",
           req.ReqType, req.SeNr, req.FlNr);
    return &req;
}

/**
 * sendAnswer() - Sendet eine Antwort an den Client
 * 
 * Benutzt: sendto() mit der gespeicherten Client-Adresse
 * 
 * Rückgabewert:
 * 0: Erfolg
 * <0: Fehler
 */
int sendAnswer(struct answer *answerPtr)
{
    ssize_t n;
    
    if (sockfd == -1) return -1;
    
    // Sendto: UDP-Paket senden
    n = sendto(sockfd,                          // Socket
            answerPtr,                          // Buffer
            sizeof(*answerPtr),                 // Größe
            0,                                  // Flags
            (struct sockaddr *)&client_addr,    // Client-Adresse (von getRequest)
            client_addr_len);                   // Länge
    
    if (n == -1) {
        perror("sendto");
        return -1;
    }
    
    // Debug-Ausgabe
    printf("[SERVER] SEND: type=%c seq/err=%lu\n",
           answerPtr->AnswType, answerPtr->SeNo);
    return 0;   // Erfolg
}

/**
 * exitServer() - Cleanup beim Beenden des Servers
 * 
 * Schließt Socket und setzt Zustandsvariablen zurück.
 */
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

/**
 * processRequest() - Verarbeitet ein empfangenes Request-Paket
 * 
 * Implementiert die Empfänger-Logik:
 * - Hello: Transfer starten
 * - Data: In-Order prüfen, Callback aufrufen, ACK senden
 * - Close: Transfer beenden
 * 
 * Parameter:
 * - reqPtr: Pointer auf Request-Paket
 * - answPtr: Pointer auf Answer-Struktur (wird ausgefüllt)
 * - lossReq: Request-Verlustrate zum Simulieren
 * 
 * Rückgabewert:
 * - Pointer auf Answer (eingefüllt)
 * - NULL: Paket wurde verworfen (Paketverlust)
 */
static struct answer *processRequest(struct request *reqPtr,
                                     struct answer *answPtr,
                                     double lossReq)
{
    static struct answer answ;  // Statisch: Answer wird zurückgegeben
    
    // Paketverlust simulieren
    if (lossReq > 0.0) {
        double r = (double)rand() / RAND_MAX;   // Zufallszahl 0.0..1.0
        if (r < lossReq) {
            // Paket verwerfen (simuliert Paketverlust)
            printf("[SERVER] LOSS: Packet loss simulated (seq=%lu)\n", reqPtr->SeNr);
            return NULL;  // Paket wird ignoriert
        }
    }
    
    // Verarbeiten der Answer-Struktur
    memset(&answ, 0, sizeof(answ));
    answ.FlNr = 0;
    
    // Request-Type verarbeiten
    switch (reqPtr->ReqType) {

        // HELLO: Transfer startet
        case ReqHello:
            printf("[SERVER] HELLO received\n");
            nextExpected = 0;   // Zurücksetzen
            transferActive = 0;
            
            // Callback: Transfer startet
            if (appStartCallback != NULL) {
                if (appStartCallback() != 0) {
                    // Fehler beim Öffnen der Datei
                    answ.AnswType = AnswErr;
                    answ.ErrNo = ERR_FILE_ERROR;
                    fprintf(stderr, "[SERVER] File open failed\n");
                    break;
                }
                transferActive = 1; // Transfer ist aktiv
            }
            
            // HELLO-ACK vorbereiten
            answ.AnswType = AnswHello;
            answ.SeNo = 0;
            printf("[SERVER] HELLO accepted, nextExpected=0\n");
            break;
            
        // DATA: Datenpakete
        case ReqData:
            if (!transferActive) {
                // Fehler: DATA ohne active Transfer
                answ.AnswType = AnswErr;
                answ.ErrNo = ERR_ILLEGAL_REQUEST;
                fprintf(stderr, "[SERVER] DATA without active transfer\n");
                break;
            }
            
            // Prüfe Sequenznummer
            if (reqPtr->SeNr == nextExpected) {
                // In-Order: Dieses Paket wird erwartet
                printf("[SERVER] DATA seq=%lu INORDER\n", reqPtr->SeNr);
                
                // Callback: Daten schreiben
                if (appWriteCallback != NULL) {
                    if (appWriteCallback(reqPtr->name, reqPtr->FlNr) != 0) {
                        // Fehler beim Schreiben
                        answ.AnswType = AnswErr;
                        answ.ErrNo = ERR_FILE_ERROR;
                        fprintf(stderr, "[SERVER] Write failed\n");
                        break;
                    }
                }
                
                // Nächstes Paket erwarten
                nextExpected++;

                // Kumulative ACK: SeNo = nächstes erwartetes Paket
                answ.AnswType = AnswOk;
                answ.SeNo = nextExpected;  /* Kumulative ACK */
                printf("[SERVER] ACK: next expected = %lu\n", nextExpected);
                
            } else if (reqPtr->SeNr < nextExpected) {
                // Duplikat: Dieses Paket wurde schon empfangen
                answ.AnswType = AnswOk;
                answ.SeNo = nextExpected;
                printf("[SERVER] DATA seq=%lu DUPLICATE, sending ACK %lu\n",
                       reqPtr->SeNr, nextExpected);
                
            } else {
                // Out-of-Order: Paket kommt zu früh -> DROP
                answ.AnswType = AnswWarn;
                answ.ErrNo = ERR_WRONG_SEQ;
                printf("[SERVER] DATA seq=%lu OUT-OF-ORDER (expected %lu) - DROPPED\n",
                       reqPtr->SeNr, nextExpected);
            }
            break;
            
        // CLOSE: Transfer endet
        case ReqClose:
            printf("[SERVER] CLOSE received\n");

            // Callback: Transfer endet
            if (transferActive && appEndCallback != NULL) {
                appEndCallback();
                transferActive = 0;
            }

            // CLOSE-ACK vorbereiten
            answ.AnswType = AnswOk;
            answ.SeNo = nextExpected;
            printf("[SERVER] CLOSE accepted\n");
            break;
            
        default:
            // Unbekannter Request-Type
            answ.AnswType = AnswErr;
            answ.ErrNo = ERR_ILLEGAL_REQUEST;
            fprintf(stderr, "[SERVER] Unknown request type: %c\n", reqPtr->ReqType);
            break;
    }
    
    // Ausgefüllte Answer in Parameter speichern
    if (answPtr != NULL) {
        *answPtr = answ;
    }
    
    return &answ;
}

/* --------------------------------------------------------------- */
/*  ARQ-Server-Hauptschleife                                       */
/* --------------------------------------------------------------- */

/**
 * arqServerLoop() - Hauptschleife des ARQ-Servers
 * 
 * Aufgaben:
 * 1. Auf Requests vom Client warten (mit select)
 * 2. Requests verarbeiten (Sequenznummern prüfen)
 * 3. ACK senden
 * 4. Bei CLOSE beenden
 * 
 * Parameter:
 * - port: Portnummer
 * - lossReq: Request-Verlustrate (für Simulation)
 * - lossAck: ACK-Verlustrate (für Simulation)
 * - appStart/appWrite/appEnd: Anwendungs-Callbacks
 * 
 * Rückgabewert:
 * 0: Erfolg
 * <0: Fehler
 */
int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd)
{
    struct request *req;
    struct answer answ;
    
    // Speichere Callbacks in globalen Variablen
    appStartCallback = appStart;
    appWriteCallback = appWrite;
    appEndCallback = appEnd;
    
    // Zufallszahlengenerator für Verlust-Simulation initialisieren
    srand(time(NULL));
    
    // Initialisiere Server (erstelle Socket, binde Port)
    if (initServer(port) != 0) {
        return -1;
    }
    
    printf("[SERVER] Main loop started\n");

    /* --------------------------------------------------------------- */
    /*  Endlosschleife: Requests empfangen und verarbeiten             */
    /* --------------------------------------------------------------- */

    while (1) {
        // Mit select() auf Requests warten (mit 100ms Timeout)
        struct timeval tv;
        fd_set readfds;
        
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms
        
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret < 0) {
            perror("select");
            break;
        }
        
        if (ret == 0) {
            // Timeout - keine Daten verfügbar
            continue;   // Nächste Iteration
        }

        // Daten verfügbar: Request empfangen
        req = getRequest();
        if (req == NULL) {
            continue;
        }

        // Request verarbeiten (Sequenznummer prüfen)
        struct answer *answPtr = processRequest(req, &answ, lossReq);
        if (answPtr == NULL) {
            // Request wurde verworfen (Paketverlust-Simulation)
            continue;   // keine ACK senden
        }
        
        /* --------------------------------------------------------------- */
        /*  ACK-Verlust simulieren                                         */
        /* --------------------------------------------------------------- */

        if (lossAck > 0.0) {
            double r = (double)rand() / RAND_MAX;
            if (r < lossAck) {
                // ACK verwerfen (simuliert ACK-Verlust)
                printf("[SERVER] LOSS: ACK loss simulated (type=%c)\n", answ.AnswType);
                continue;  // ACK wird nicht gesendet
            }
        }

        // ACK senden
        if (sendAnswer(&answ) != 0) {
            fprintf(stderr, "[SERVER] Error sending answer\n");
        }

        // Close-Handshake: Transfer beenden?
        if (req->ReqType == ReqClose && answ.AnswType == AnswOk) {
            printf("[SERVER] Transfer complete, exiting\n");
            break;  // Hauptschleife beenden
        }
    }
    
    // Cleanup
    exitServer();
    return 0;
}