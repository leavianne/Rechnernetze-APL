# Makefile für ARQ-Client/Server Projekt
# braucht man nicht unbedingt - manuell kompilieren geht schneller und ist sicherer, das hier ist nur fancy

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -g
LDFLAGS =

# Standartwerte
PORT = 3333
SERVER = ::1
WINDOW = 3
IMPUTFILE = input.txt
OUTPUTFILE = output.txt

# Ziele
all: server client

# --- Server ---
server: server.o serverSy.o error.o
	$(CC) $(CFLAGS) -o $@ $^$(LDFLAGS)

server.o: server.c serverSy.h data.h config.h
	$(CC) $(CFLAGS) -c $<

serverSy.o: serverSy.c serverSy.h data.h config.h
	$(CC) $(CFLAGS) -c $<

# --- Client ---
client: client.o clientSy.o error.o 
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client.o: client.c clientSy.h data.h config.h
	$(CC) $(CFLAGS) -c $<

clientSy.o: clientSy.c clientSy.h data.h config.h
	$(CC) $(CFLAGS) -c $<

# --- Gemeinsame Dateien ---
error.o: error.c data.h
	$(CC) $(CFLAGS) -c $<

# --- Testdatei erstellen ---
testfile:
	@echo "Erstelle Testdatei $(INPUTFILE)..."
	@for i in {1..20}; do echo "Zeile $$i: Das ist ein Test für das ARQ-Protokoll mit Go-Back-N"; done > $(INPUTFILE)
	@echo "Testdatei mit 20 Zeichen erstellt: $(INPUTFILE)"

# --- Clean ---
clean:
	rm -f *.o server client $(OUTPUTFILE)

distclean: clean
	rm -f $(INPUTFILE) $(OUTPUTFILE)

# --- Testläufe ---
run-server-basic:
	./server -p $(PORT) -f $(OUTPUTFILE)

run-server-loss:
	./server -p $(PORT) -f $(OUTPUTFILE) -r 0.2 -a 0.1

run-client-basic:
	./client -a $(SERVER) -p $(PORT) -f $(INPUTFILE) -w $(WINDOW)

run-client-w1:
	./client -a $(SERVER) -p $(PORT) -f $(INPUTFILE) -w 1

run-client-w5:
	./client -a $(SERVER) -p $(PORT) -f $(INPUTFILE) -w 5

# --- Kompletter Test ---
test: all testfile
	@echo "Starte Server im Hintergrund..."
	@./server -p $(PORT) -f $(OUTPUTFILE) &
	@sleep 2
	@echo "Starte Client..."
	@./client -a $(SERVER) -p $(PORT) -f $(INPUTFILE) -w $(WINDOW)
	@killall server 2>/dev/null || true
	@echo "Vergleiche Dateien..."
	@if diff $(INPUTFILE) $(OUTPUTFILE) >/dev/null; then \
		echo "ERFOLG: Dateien sind identisch!"; \
	else \
		echo "FEHLER: Dateien unterscheiden sich!"; \
		diff $(INPUTFILE) $(OUTPUTFILE) | head -20; \
	fi

# --- Hilfe ---
help:
	@echo "Verfügbare Ziele:"
	@echo "all			- Kompiliert Server und Client"
	@echo "server		- Kompiliert nur den Server"
	@echo "client		- Kompiliert nur den Client"
	@echo "clean		- Löscht Objekt- und Programmdateien"
	@echo "distclean	- Löscht alles inkl. Testdateien"
	@echo "testfile 	- Erstellt Testdatei"
	@echo "test			- Führt kompletten Test durch"
	@echo "run-server-*	- Startet Server mit verschiedenen Optionen"
	@echo "run-client-*	- Startet Client mit verschiedenen Optionen"
	@echo ""
	@echo "Beispiel für manuellen Start:"
	@echo "Terminal 1: ./server -p 3333 -f output.txt"
	@echo "Terminal 2: ./client -a ::1 -p 3333 -f input.txt -w 3"


.PHONY: all clean distclean testfile test help run-server-basic run-server-loss run-client-basic run-client-w1 run-client-w5
