#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

// Anwendungszustand: Ausgabedatei
static const char *gOutputFile = NULL;
static FILE       *gFp         = NULL;
static int         gFileOk     = 0;

static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>]\n",
            progName);
    fprintf(stderr, "   -p <port>    : Server-Port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "   -f <outfile> : Ausgabedatei\n");
    fprintf(stderr, "   -r <lossReq> : Request-Verlustwahrscheinlichkeit (0.0..1.0)\n");
    fprintf(stderr, "   -a <lossAck> : ACK-Verlustwahrscheinlichkeit (0.0..1.0)\n");
    exit(EXIT_FAILURE);
}

// Anwendungscallbacks für die ARQ-Schicht

// Ausgabedatei öffnen/neu anlegen.
static int appStartTransfer(void)
{
    gFileOk = 0;

    if (!gOutputFile) {
        fprintf(stderr, "Server: Keine Ausgabedatei angegeben.\n");
        return -1;
    }

    gFp = fopen(gOutputFile, "w");
    if (gFp == NULL) {
        perror("fopen");
        fprintf(stderr, "Server: Konnte Datei '%s' nicht öffnen\n", gOutputFile);
        return -1;
    }

    gFileOk = 1;
    printf("Server: Starte Transfer -> Schreibe in '%s'\n", gOutputFile);
    return 0;
}

// Nutzdaten in Datei schreiben.
static int appWriteData(const char *buf, unsigned long len)
{
    if (!gFileOk || gFp == NULL) {
        fprintf(stderr, "Server: Datei nicht bereit zum Schreiben\n");
        return -1;
    }

    size_t written = fwrite(buf, 1, len, gFp);
    if (written != len) {
        perror("fwrite");
        fprintf(stderr, "Server: Fehler beim Schreiben in Datei\n");
        return -1;
    }

    // Zeilenumbruch hinzufügen, da readAppUnit diesen entfernt
    if (fputc('\n', gFp) == EOF) {
        perror("fputc");
        return -1;
    }

    fflush(gFp);  // Sofort schreiben für bessere Sichtbarkeit
    printf("Server: Schreibe %lu Bytes in Datei\n", len);
    return 0;
}

// Datei schließen.
static void appEndTransfer(void)
{
    if (gFp != NULL) {
        fclose(gFp);
        gFp = NULL;
    }
    
    gFileOk = 0;
    printf("Server: Transfer beendet, Datei geschlossen\n");
}

// --- main: Argumente auswerten, ARQ-Schicht starten ---

int main(int argc, char *argv[])
{
    const char *port = DEFAULT_PORT;
    double lossReq   = 0.0;
    double lossAck   = 0.0;
    long i;

    // Programmargumente auswerten
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                case 'p': // Server-Port
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f': // Ausgabedatei
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        gOutputFile = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'r': // Request-Verlust
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossReq = atof(argv[++i]);
                        if (lossReq < 0.0 || lossReq > 1.0) {
                            fprintf(stderr, "lossReq muss zwischen 0.0 und 1.0 liegen\n");
                            exit(EXIT_FAILURE);
                        }
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'a': // ACK-Verlust
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossAck = atof(argv[++i]);
                        if (lossAck < 0.0 || lossAck > 1.0) {
                            fprintf(stderr, "lossAck muss zwischen 0.0 und 1.0 liegen\n");
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

    if (!gOutputFile) {
        usage(argv[0]);
    }

    printf("Server: Höre auf Port %s\n", port);
    printf("Server: Ausgabedatei: %s\n", gOutputFile);
    printf("Server: lossReq = %.2f, lossAck = %.2f\n", lossReq, lossAck);

    if (arqServerLoop(port, lossReq, lossAck,
                      appStartTransfer, appWriteData, appEndTransfer) < 0) {
        fprintf(stderr, "Server: arqServerLoop fehlgeschlagen\n");
        return EXIT_FAILURE;
    }

    printf("Server: Erfolgreich beendet\n");
    return EXIT_SUCCESS;
}
