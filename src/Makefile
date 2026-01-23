# ============================================================
# Makefile für ARQ Go-Back-N Client/Server
# 
# Einfache Version mit nur 2 Zielen:
#   - make       : Kompiliert Client und Server
#   - make input : Erstellt input.txt
# ============================================================

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -g

# ============================================================
# ZIELE
# ============================================================

all: server client input.txt

# --- Server kompilieren ---
server: server.o serverSy.o error.o
	$(CC) $(CFLAGS) -o server server.o serverSy.o error.o

server.o: server.c serverSy.h data.h config.h
	$(CC) $(CFLAGS) -c server.c

serverSy.o: serverSy.c serverSy.h data.h config.h
	$(CC) $(CFLAGS) -c serverSy.c

# --- Client kompilieren ---
client: client.o clientSy.o error.o
	$(CC) $(CFLAGS) -o client client.o clientSy.o error.o

client.o: client.c clientSy.h data.h config.h
	$(CC) $(CFLAGS) -c client.c

clientSy.o: clientSy.c clientSy.h data.h config.h
	$(CC) $(CFLAGS) -c clientSy.c

# --- Error Handling ---
error.o: error.c data.h
	$(CC) $(CFLAGS) -c error.c

# --- Input-Datei erstellen ---
input.txt:
	@echo "Erstelle input.txt..."
	@echo "Hallo Amelie" > input.txt
	@echo "Hallo Anton" >> input.txt
	@echo "Hallo Iryna" >> input.txt
	@echo "Hallo Lea" >> input.txt
	@echo "input.txt erstellt mit 4 Zeilen"

# --- Aufräumen ---
clean:
	rm -f *.o server client output.txt

distclean: clean
	rm -f input.txt

# --- Info ---
.PHONY: all clean distclean