#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

static const char *gOutputFile = NULL;
static FILE       *gFp         = NULL;
static int         gFileOk     = 0;

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

static int appStartTransfer(void)
{
    gFileOk = 0;

    if (!gOutputFile) {
        fprintf(stderr, "[SERVER] No output file specified\n");
        return -1;
    }

    gFp = fopen(gOutputFile, "w");
    if (gFp == NULL) {
        perror("fopen");
        return -1;
    }

    gFileOk = 1;
    printf("[SERVER] Transfer started, writing to: %s\n", gOutputFile);
    return 0;
}

static int appWriteData(const char *buf, unsigned long len)
{
    if (!gFileOk || gFp == NULL) {
        fprintf(stderr, "[SERVER] File not ready\n");
        return -1;
    }

    size_t written = fwrite(buf, 1, len, gFp);
    if (written != len) {
        perror("fwrite");
        return -1;
    }

    if (fputc('\n', gFp) == EOF) {
        perror("fputc");
        return -1;
    }

    fflush(gFp);
    printf("[SERVER] Wrote %lu bytes (line)\n", len);
    return 0;
}

static void appEndTransfer(void)
{
    if (gFp != NULL) {
        fclose(gFp);
        gFp = NULL;
    }
    gFileOk = 0;
    printf("[SERVER] Transfer ended, file closed\n");
}

int main(int argc, char *argv[])
{
    const char *port = DEFAULT_PORT;
    double lossReq   = 0.0;
    double lossAck   = 0.0;
    long i;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {
                case 'p':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        gOutputFile = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'r':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossReq = atof(argv[++i]);
                        if (lossReq < 0.0 || lossReq > 1.0) {
                            fprintf(stderr, "Loss rate must be 0.0..1.0\n");
                            exit(EXIT_FAILURE);
                        }
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'a':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossAck = atof(argv[++i]);
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

    if (!gOutputFile) {
        usage(argv[0]);
    }

    printf("[SERVER] ====================================\n");
    printf("[SERVER] Listening on port: %s\n", port);
    printf("[SERVER] Output file: %s\n", gOutputFile);
    printf("[SERVER] Loss: request=%.1f%%, ACK=%.1f%%\n", lossReq * 100, lossAck * 100);
    printf("[SERVER] ====================================\n");

    if (arqServerLoop(port, lossReq, lossAck,
                      appStartTransfer, appWriteData, appEndTransfer) < 0) {
        fprintf(stderr, "[SERVER] Server loop failed\n");
        return EXIT_FAILURE;
    }

    printf("[SERVER] Exited successfully\n");
    return EXIT_SUCCESS;
}