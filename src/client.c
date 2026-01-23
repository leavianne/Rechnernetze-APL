#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

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

static int readAppUnit(struct app_unit *app, FILE *f)
{
    size_t len;
    char *res;

    if (app == NULL || f == NULL) {
        return -1;
    }

    res = fgets(app->data, BufferSize, f);
    if (res == NULL) {
        if (feof(f)) {
            return 0;  /* EOF */
        }
        return -1;  /* Error */
    }

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
    int windowSize         = 1;

    FILE *fp = NULL;
    long i;

    /* Parse command line */
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {
                case 'a':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        server = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'p':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        filename = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'w':
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

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return EXIT_FAILURE;
    }

    printf("[CLIENT] ====================================\n");
    printf("[CLIENT] File: %s\n", filename);
    printf("[CLIENT] Server: %s:%s\n", server ? server : "loopback", port);
    printf("[CLIENT] Window size: %d\n", windowSize);
    printf("[CLIENT] ====================================\n");

    initClient((char *)server, port);

    if (arqSendHello(windowSize) != 0) {
        fprintf(stderr, "[CLIENT] HELLO failed\n");
        if (fp) fclose(fp);
        closeClient();
        return EXIT_FAILURE;
    }

    struct app_unit app;
    int r;
    int packet_count = 0;
    
    while (1) {
        r = readAppUnit(&app, fp);
        if (r < 0) {
            fprintf(stderr, "[CLIENT] Read error\n");
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        } else if (r == 0) {
            printf("[CLIENT] EOF reached\n");
            break;
        }
        
        printf("[CLIENT] Sending line %d: '%s'\n", packet_count + 1, app.data);
        
        if (arqSendData(&app, windowSize) != 0) {
            fprintf(stderr, "[CLIENT] Send error for packet %d\n", packet_count);
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        }
        
        packet_count++;
    }

    printf("[CLIENT] Sent %d packets total\n", packet_count);

    if (arqSendClose(windowSize) != 0) {
        fprintf(stderr, "[CLIENT] CLOSE failed\n");
    }

    if (fp) fclose(fp);
    closeClient();

    printf("[CLIENT] Transfer complete\n");
    return EXIT_SUCCESS;
}