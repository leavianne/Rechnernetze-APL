#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

/* --------------------------------------------------------------- */
/*  Globale Variablen - Anwendungszustand                          */
/* --------------------------------------------------------------- */

static const char *gOutputFile = NULL;  // Name der Ausgabedatei (wird über -f übergeben)
static FILE       *gFp         = NULL;  // FILE* der Ausgabedatei (geöffnet während Transfer)
static int         gFileOk     = 0;     // Flag: ist die Datei erfolgreich geöffnet?

/* --------------------------------------------------------------- */
/*  Hilfsfunktionen - Argument-Verarbeitung                        */
/* --------------------------------------------------------------- */

/**
 * usage() - Gibt Verwendungsinfo aus und beendet das Programm
 * 
 * Parameter:
 * progName: Programmname (argv[0])
 */
static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>]\n",
            progName);
    fprintf(stderr, "   -p <port>    : Server port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "   -f <outfile> : Output file\n");
    fprintf(stderr, "   -r <lossReq> : Request loss rate (0.0..1.0)\n");
    fprintf(stderr, "   -a <lossAck> : ACK loss rate (0.0..1.0)\n");
    exit(EXIT_FAILURE);
}

/* --------------------------------------------------------------- */
/*  Anwendungs-Callbacks - Werden von der ARQ-Schicht aufgerufen   */
/* --------------------------------------------------------------- */

/**
 * appStartTransfer() - Callback: Transfer startet
 * 
 * Aufgerufen von ARQ-Schicht, wenn HELLO-Paket empfangen wird.
 * Öffnet die Ausgabedatei zum Schreiben.
 * 
 * Rückgabewert:
 * 0: Erfolg (Datei geöffnet)
 * <0: Fehler (Datei konnte nicht geöffnet werden)
 */
static int appStartTransfer(void)
{
    gFileOk = 0;    // Rücksetzen

    if (!gOutputFile) {
        fprintf(stderr, "[SERVER] No output file specified\n");
        return -1;
    }

    // Öffne Aufgabedatei zum Schreiben
    gFp = fopen(gOutputFile, "w");
    if (gFp == NULL) {
        perror("fopen");
        return -1;
    }

    gFileOk = 1;    // Flag: Datei erfolgreich geöffnet
    printf("[SERVER] Transfer started, writing to: %s\n", gOutputFile);
    return 0;   // Erfolg
}

/**
 * appWriteData() - Callback: Daten in Datei schreiben
 * 
 * Aufgerufen von ARQ-Schicht, wenn ein In-Order Datenpaket empfangen wird.
 * 
 * Parameter:
 * buf: Zeiger auf Nutzdaten
 * len: Länge der Nutzdaten in Bytes
 * 
 * Rückgabewert:
 * 0: Erfolg
 * <0: Fehler (z.B. Schreibfehler)
 */
static int appWriteData(const char *buf, unsigned long len)
{
    if (!gFileOk || gFp == NULL) {
        fprintf(stderr, "[SERVER] File not ready\n");
        return -1;
    }

    // Schreibe len Bytes aus buf in die Datei
    size_t written = fwrite(buf, 1, len, gFp);
    if (written != len) {
        perror("fwrite");
        return -1;
    }

    // füge Zeilenumbruch hinzu (readAppUnit() hat ihn entfernt)
    if (fputc('\n', gFp) == EOF) {
        perror("fputc");
        return -1;
    }

    // Sofort in Datei schreiben (nicht puffern)
    fflush(gFp);
    printf("[SERVER] Wrote %lu bytes (line)\n", len);
    return 0;   // Erfolg
}

/**
 * appEndTransfer() - Callback: Transfer beenden
 * 
 * Aufgerufen von ARQ-Schicht, wenn CLOSE-Paket empfangen wird.
 * Schließt die Ausgabedatei.
 */
static void appEndTransfer(void)
{
    if (gFp != NULL) {
        fclose(gFp);
        gFp = NULL;
    }
    gFileOk = 0;    // Flag zurücksetzen
    printf("[SERVER] Transfer ended, file closed\n");
}

/* --------------------------------------------------------------- */
/*  Hauptprogramm                                                  */
/* --------------------------------------------------------------- */

/**
 * main() Einstiegspunkt
 * 
 * Aufgaben:
 * 1. Kommandozeilenargumente parsen
 * 2. ARQ-Server mit Callbacks aufrufen
 * 3. Fehler behandeln
 */
int main(int argc, char *argv[])
{
    const char *port = DEFAULT_PORT;    // Portnummer (default: "3333")
    double lossReq   = 0.0;             // Request-Verlustrate (0.0 = kein Verlust)
    double lossAck   = 0.0;             // ACK-Verlustrate (0.0 = kein Verlust)
    long i;

    /* --------------------------------------------------------------- */
    /*  Kommandozeilenparameter parsen                                 */
    /* --------------------------------------------------------------- */

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            // Prüfe ob Argument mit '-' oder '/' anfängt und 2 Zeichen lang ist
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                // Welcher Parameter?
                switch (tolower((unsigned char)argv[i][1])) {

                // -p <port>
                case 'p':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];   // Nächstes Argument ist Port
                        break;
                    }
                    usage(argv[0]);
                    break;

                // -f <outfile>
                case 'f':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        gOutputFile = argv[++i];  // Nächstes Argument ist Dateiname
                        break;
                    }
                    usage(argv[0]);
                    break;

                // -r <lossReq>
                case 'r':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossReq = atof(argv[++i]);  // Konvertiere zu double

                        if (lossReq < 0.0 || lossReq > 1.0) {
                            fprintf(stderr, "Loss rate must be 0.0..1.0\n");
                            exit(EXIT_FAILURE);
                        }
                        break;
                    }
                    usage(argv[0]);
                    break;

                // -a <lossAck>
                case 'a':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossAck = atof(argv[++i]);  // Konvertiere zu double

                        if (lossAck < 0.0 || lossAck > 1.0) {
                            fprintf(stderr, "Loss rate must be 0.0..1.0\n");
                            exit(EXIT_FAILURE);
                        }
                        break;
                    }
                    usage(argv[0]);
                    break;

                default:
                    usage(argv[0]);
                    break;
                }
            } else {
                usage(argv[0]);
            }
        }
    }

    // Prüfe ob erforderliche Argumente gesetzt wurden
    if (!gOutputFile) {
        usage(argv[0]);
    }

    /* --------------------------------------------------------------- */
    /*  Banner                                                         */
    /* --------------------------------------------------------------- */

    printf("[SERVER] ====================================\n");
    printf("[SERVER] Listening on port: %s\n", port);
    printf("[SERVER] Output file: %s\n", gOutputFile);
    printf("[SERVER] Loss: request=%.1f%%, ACK=%.1f%%\n", lossReq * 100, lossAck * 100);
    printf("[SERVER] ====================================\n");

    /* --------------------------------------------------------------- */
    /*  ARQ-Server-Hauptschleife starten                               */
    /* --------------------------------------------------------------- */

    /**
     * Callbacks übergeben:
     * - appStartTransfer: wenn HELLO empfangen
     * - appWriteData: wenn In-Order DATA empfangen
     * - appEndTransfer: wenn CLOSE empfangen
     */
    if (arqServerLoop(port, lossReq, lossAck,
                      appStartTransfer, appWriteData, appEndTransfer) < 0) {
        fprintf(stderr, "[SERVER] Server loop failed\n");
        return EXIT_FAILURE;
    }

    printf("[SERVER] Exited successfully\n");
    return EXIT_SUCCESS;
}