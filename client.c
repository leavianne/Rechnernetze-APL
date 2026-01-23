#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

/* usage-Ausgabe */
static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -a <server> -p <port> -f <file> -w <window>\n", progName);
    fprintf(stderr, "       -a <server> : Server-Adresse (Default: %s)\n",
            (DEFAULT_SERVER == NULL) ? "loopback" : DEFAULT_SERVER);
    fprintf(stderr, "       -p <port>   : Server-Port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "       -f <file>   : Eingabedatei\n");
    fprintf(stderr, "       -w <window> : Fenstergröße (1..10)\n");
    exit(EXIT_FAILURE);
}

static int readAppUnit(struct app_unit *app, FILE *f)
{
    size_t len;
    char *res;

    if (app == NULL || f == NULL) {
        return -1;
    }

    /* Zeile lesen */
    res = fgets(app->data, BufferSize, f);
    if (res == NULL) {
        if (feof(f)) {
            return 0; /* EOF */
        }
        /* Fehler beim Lesen */
        return -1;
    }

    /* Länge bestimmen und Newline entfernen */
    len = strlen(app->data);
    if (len > 0 && app->data[len - 1] == '\n') {
        app->data[len - 1] = '\0';
        len--;
    }

    app->len = (unsigned long)len;
    return (int)len;
}

int main(int argc, char *argv[])
{
    const char *server     = DEFAULT_SERVER;
    const char *filename   = NULL;
    const char *port       = DEFAULT_PORT;
    int windowSize         = 1;  /* Standard: Stop-and-Wait */

    FILE *fp = NULL;
    long i;

    /* Kommandozeilenparameter auswerten */
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {
                case 'a': /* Server-Adresse */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        server = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'p': /* Server-Port */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f': /* Eingabedatei */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        filename = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'w': /* Fenstergröße */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        windowSize = atoi(argv[++i]);
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

    if (!filename) {
        usage(argv[0]);
    }

    /* Datei öffnen */
    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return EXIT_FAILURE;
    }

    printf("Client: Sending file '%s' to %s:%s (window=%d)\n", 
           filename, server, port, windowSize);

    /* ARQ-Client initialisieren */
    initClient((char *)server, port);

    /* Hello/Verbindungsaufbau */
    if (arqSendHello(windowSize) != 0) {
        fprintf(stderr, "Client: Hello failed, aborting.\n");
        if (fp) fclose(fp);
        closeClient();
        return EXIT_FAILURE;
    }

    /* Datei zeilenweise lesen und senden */
    struct app_unit app;
    int r;
    
    while (1) {
        r = readAppUnit(&app, fp);
        if (r < 0) {
            fprintf(stderr, "Client: Error while reading file.\n");
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        } else if (r == 0) {
            break; /* EOF */
        }
        
        /* Nachricht senden */
        if (arqSendData(&app, windowSize) != 0) {
            fprintf(stderr, "Client: Error while sending data.\n");
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        }
    }

    /* Close / Verbindungsabbau */
    if (arqSendClose(windowSize) != 0) {
        fprintf(stderr, "Client: Error while sending close.\n");
    }

    /* Aufräumen */
    if (fp) fclose(fp);
    closeClient();

    printf("Client: Beendet\n");
    return EXIT_SUCCESS;
}
