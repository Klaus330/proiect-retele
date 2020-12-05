#fisier folosit pentru compilarea serverului&clientului UDP

	gcc -pthread servTcpPreTh.c -o server -lsqlite3 -std=c99
	gcc cliTcpNr.c -o client

