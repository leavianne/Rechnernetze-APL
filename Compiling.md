# Zum Compilen:

gcc -Wall -Wextra -o error.c

gcc -Wall -Wextra -o server serverSy.c server.c 

gcc -Wall -Wextra -o client clientSy.c client.c 

## wichtig: 
KEIN -std=99 (es bricht alles for some reason)
bei error.c könnte auch was anderes statt "-o" sein, idk mehr (forgor)

## SEHR WICHTIG:
ZUERST clientSy.c oder serverSy.c, dann die andere ohne Sy - ansonsten geht die andere kaputt
am besten alles vorher speichern und zum Testen eine Kopie benutzen um Verluste zu vermeiden

## zum Ausführen:
Server:
./server -p 3333 -f output.txt
Client:
./client -a ::1 -p 3333 -f input.txt (es muss eine input.txt (nicht leer) datei schon existieren (es geht auch test.txt oder was auch immer sonst))

## was auch geht:
./server und ./client - zeigen nur so allgemeine Infos an
