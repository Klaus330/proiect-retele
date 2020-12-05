#fisier folosit pentru compilarea serverului&clientului UDP

all:
	gcc -pthread servTcpPreTh.c -o server
	gcc cliTcpNr.c -o client
clean:
	rm -f *~server client
