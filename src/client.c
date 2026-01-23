#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

/* --------------------------------------------------------------- */
/*  Hilfsfunktionen                                                */
/* --------------------------------------------------------------- */

/**
 * usage() - Gibt Verwendungsinfo aus und beendet das Programm
 * 
 * Parameter:
 * progName: Programmname (argv[0])
 */
static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -a <server> -p <port> -f <file> -w <window>\n", progName);
    fprintf(stderr, "       -a <server> : Server address (Default: %s)\n",
            (DEFAULT_SERVER == NULL) ? "loopback" : DEFAULT_SERVER);
    fprintf(stderr, "       -p <port>   : Server port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "       -f <file>   : Input file\n");
    fprintf(stderr, "       -w <window> : Window size (1..10, Default: 1)\n");
    exit(EXIT_FAILURE);
}

/**
 * readAppUnit() - Liest eine Zeile aus Datei
 * 
 * Aufgaben:
 * - Zeile mit fgets() lesen
 * - Newline entfernen
 * - app->len und app->data setzen
 * 
 * Parameter:
 * - app: Zeiger auf app_unit Struktur
 * - f: Zeiger auf offene Datei
 * 
 * Rückgabewert:
 * > 0: Erfolg (Anzahl bytes gelesen)
 * 0: EOF (Dateiende)
 * < 0: Fehler
 */
static int readAppUnit(struct app_unit *app, FILE *f)
{
    size_t len;
    char *res;

    // Validierung
    if (app == NULL || f == NULL) {
        return -1;
    }

    // Zeile lesen mit fgets()
    res = fgets(app->data, BufferSize, f);
    if (res == NULL) {
        if (feof(f)) {
            return 0;  // EOF: Dateiende erreicht
        }
        return -1;  // Fehler beim Lesen
    }

    // Länge bestimmen und Newline entfernen
    len = strlen(app->data);
    if (len > 0 && app->data[len - 1] == '\n') {
        app->data[len - 1] = '\0';  // Newline entfernen
        len--;
    }

    // Nutzdaten-Länge speichern
    app->len = (unsigned long)len;
    return (int)len;    // Erfolg: Anzahl gelesener Bytes
}

/* --------------------------------------------------------------- */
/*  Hauptprogramm                                                  */
/* --------------------------------------------------------------- */

/**
 * main() - Einstiegspunkt des Clients
 * 
 * Aufgaben:
 * 1. Kommandozeilenargumente parsen
 * 2. Eingabedatei öffnen
 * 3. Client initialisieren
 * 4. HELLO senden
 * 5. Dateiinhalt zeilenweise senden
 * 6. CLOSE senden
 * 7. Cleanup
 * 
 * Rückgabewert:
 * EXIT_SUCCESS: Erfolg
 * EXIT_FAILURE: Fehler
 */
int main(int argc, char *argv[])
{
    const char *server     = DEFAULT_SERVER;    // Servername/IP
    const char *filename   = NULL;              // Eingabedatei
    const char *port       = DEFAULT_PORT;      // Portnummer
    int windowSize         = 1;                // Go-Back-N Fenstergröße

    FILE *fp = NULL;
    long i;

    /* --------------------------------------------------------------- */
    /*  Kommandozeilenparameter parsen                                 */
    /* --------------------------------------------------------------- */

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            // Prüfe ob Argument mit '-' oder '/' anfängt und 2 Zeichen lang ist
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                // -a <server>
                case 'a':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        server = argv[++i]; // Nächstes Argument ist Servername
                        break;
                    }
                    usage(argv[0]);
                    break;

                // -p <port>
                case 'p':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];   // Nächstes Argument ist Port
                        break;
                    }
                    usage(argv[0]);
                    break;

                // -f <file>
                case 'f':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        filename = argv[++i];   // Nächstes Argument ist Dateiname
                        break;
                    }
                    usage(argv[0]);
                    break;

                // -w <window>
                case 'w':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        windowSize = atoi(argv[++i]);   // Konvertiere zu int

                        if (windowSize < 1 || windowSize > 10) {
                            fprintf(stderr, "Window size must be 1..10\n");
                            usage(argv[0]);
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
    if (!filename) {
        usage(argv[0]);
    }

    /* --------------------------------------------------------------- */
    /*  Eingabedatei öffnen                                            */
    /* --------------------------------------------------------------- */

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return EXIT_FAILURE;
    }

    /* --------------------------------------------------------------- */
    /*  Banner                                                         */
    /* --------------------------------------------------------------- */

    printf("[CLIENT] ====================================\n");
    printf("[CLIENT] File: %s\n", filename);
    printf("[CLIENT] Server: %s:%s\n", server ? server : "loopback", port);
    printf("[CLIENT] Window size: %d\n", windowSize);
    printf("[CLIENT] ====================================\n");

    /* --------------------------------------------------------------- */
    /*  Client initialisieren                                          */
    /* --------------------------------------------------------------- */

    initClient((char *)server, port);

    /* --------------------------------------------------------------- */
    /*  HELLO-Handshake (Verbindungsaufbau)                            */
    /* --------------------------------------------------------------- */

    if (arqSendHello(windowSize) != 0) {
        fprintf(stderr, "[CLIENT] HELLO failed\n");
        if (fp) fclose(fp);
        closeClient();
        return EXIT_FAILURE;
    }

    /* --------------------------------------------------------------- */
    /*  Datei zeilenweise lesen und senden                             */
    /* --------------------------------------------------------------- */

    struct app_unit app;    // Buffer für eine Zeile
    int r;                  // Rückgabewert von readAppUnit()
    int packet_count = 0;   // Zähler für Pakete
    
    while (1) {
        // Zeile aus Datei lesen
        r = readAppUnit(&app, fp);

        if (r < 0) {
            // Fehler beim Lesen
            fprintf(stderr, "[CLIENT] Read error\n");
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        } else if (r == 0) {
            // EOF erreicht
            printf("[CLIENT] EOF reached\n");
            break;  // Ende der Schleife
        }
        
        // Debug-Ausgabe
        printf("[CLIENT] Sending line %d: '%s'\n", packet_count + 1, app.data);
        
        // Zeile mit ARQ senden
        if (arqSendData(&app, windowSize) != 0) {
            fprintf(stderr, "[CLIENT] Send error for packet %d\n", packet_count);
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        }
        
        packet_count++;
    }

    printf("[CLIENT] Sent %d packets total\n", packet_count);

    /* --------------------------------------------------------------- */
    /*  CLOSE-Handshake (Verbindungsabbau)                             */
    /* --------------------------------------------------------------- */

    if (arqSendClose(windowSize) != 0) {
        fprintf(stderr, "[CLIENT] CLOSE failed\n");
    }

    /* --------------------------------------------------------------- */
    /*  Cleanup                                                        */
    /* --------------------------------------------------------------- */

    if (fp) fclose(fp);
    closeClient();

    printf("[CLIENT] Transfer complete\n");
    return EXIT_SUCCESS;
}