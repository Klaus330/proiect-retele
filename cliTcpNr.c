/* cliTCPIt.c - Exemplu de client TCP
   Trimite un nume la server; primeste de la server "Hello nume".
         
   Autor: Lenuta Alboaie  <adria@infoiasi.ro> (c)2009
*/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h> 
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sqlite3.h>
/* codul de eroare returnat de anumite apeluri */
extern int errno;
#define BUFFERSIZE 4096
/* portul de conectare la server*/
int port;

int main (int argc, char *argv[])
{
  int sd;			// descriptorul de socket
  struct sockaddr_in server;	// structura folosita pentru conectare 
  		// mesajul trimis
  char request[BUFFERSIZE];

  /* exista toate argumentele in linia de comanda? */
  if (argc != 3)
    {
      printf ("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
      return -1;
    }

  /* stabilim portul */
  port = atoi (argv[2]);

  /* cream socketul */
  if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("Eroare la socket().\n");
      return errno;
    }

  /* umplem structura folosita pentru realizarea conexiunii cu serverul */
  /* familia socket-ului */
  server.sin_family = AF_INET;
  /* adresa IP a serverului */
  server.sin_addr.s_addr = inet_addr(argv[1]);
  /* portul de conectare */
  server.sin_port = htons (port);
  
  /* ne conectam la server */
  if (connect (sd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1)
    {
      perror ("[client]Eroare la connect().\n");
      return errno;
    }
  printf("\tBun venit pe TOP MUSIC!\n Pentru a vedea comenzile disponibile tasteaza /help\n");
  while(1){
    /* citirea mesajului */
    printf (">>");
    fflush (stdout);
    bzero(request,BUFFERSIZE);
    read (0, request, BUFFERSIZE); // citeste de la tastatura
    request[strlen(request)-1]='\0'; // am scapat de \n

   
    
    printf("[client] Am citit %s\n",request);

    /* trimiterea mesajului la server */
    if (write (sd,&request,sizeof(request)) <= 0)
    {
      perror ("[client]Eroare la write() spre server.\n");
      return errno;
    }
    bzero(request,BUFFERSIZE);
    /* citirea raspunsului dat de server 
      (apel blocant pina cind serverul raspunde) */
    if (read (sd, &request, sizeof(request)) < 0)
    {
      perror ("[client]Eroare la read() de la server.\n");
      return errno;
    }
    if(strlen(request) == 0) break;
    if(strstr(request,"/quit") && !strstr(request,"<< /quit"))
    { 
      printf ("%s\n", request);      
      printf ("[client] Am incheiat conexiunea cu serverul.\n");  
      break;
    }
    /* afisam mesajul primit */
    printf ("%s\n", request);
    bzero(request,BUFFERSIZE);
  } 
  /* inchidem conexiunea, am terminat */
  close (sd);
}

