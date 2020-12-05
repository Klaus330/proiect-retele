/* servTCPPreTh.c - Exemplu de server TCP concurent care deserveste clientii
   printr-un mecanism de prethread-ing; cu blocarea mutex de protectie a lui accept(); 
   Asteapta un numar de la clienti si intoarce clientilor numarul incrementat.
  
    
   Autor: Lenuta Alboaie  <adria@infoiasi.ro> (c)2009
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

/* portul folosit */
#define PORT 2909
#define BUFFERSIZE 4096
#define SERVER_ERROR -1
/* codul de eroare returnat de anumite apeluri */
extern int errno;

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
//void raspunde(void *);

typedef struct
{
  pthread_t idThread; //id-ul thread-ului
  int thCount;        //nr de conexiuni servite
} Thread;

Thread *threadsPool; //un array de structuri Thread

int sd;                                            //descriptorul de socket de ascultare
int nthreads;                                      //numarul de threaduri
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER; // variabila mutex ce va fi partajata de threaduri

void raspunde(int cl, int idThread);

int main(int argc, char *argv[])
{
  struct sockaddr_in server; // structura folosita de server
  void threadCreate(int);

  if (argc < 2)
  {
    fprintf(stderr, "Eroare: Primul argument este numarul de fire de executie...");
    exit(1);
  }
  nthreads = atoi(argv[1]);
  if (nthreads <= 0)
  {
    fprintf(stderr, "Eroare: Numar de fire invalid...");
    exit(1);
  }
  threadsPool = calloc(sizeof(Thread), nthreads);

  /* crearea unui socket */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("[server]Eroare la socket().\n");
    return errno;
  }
  /* utilizarea optiunii SO_REUSEADDR */
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  /* pregatirea structurilor de date */
  bzero(&server, sizeof(server));

  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
  server.sin_family = AF_INET;
  /* acceptam orice adresa */
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  /* utilizam un port utilizator */
  server.sin_port = htons(PORT);

  /* atasam socketul */
  if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[server]Eroare la bind().\n");
    return errno;
  }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen(sd, 2) == -1)
  {
    perror("[server]Eroare la listen().\n");
    return errno;
  }

  printf("Nr threaduri %d \n", nthreads);
  fflush(stdout);
  int i;
  for (i = 0; i < nthreads; i++)
    threadCreate(i);

  sigaction(SIGPIPE, &(struct sigaction){SIG_IGN}, NULL);
  /* servim in mod concurent clientii...folosind thread-uri */
  for (;;)
  {
    printf("[server]Asteptam la portul %d...\n", PORT);
    pause();
  }
};

void threadCreate(int i)
{
  void *treat(void *);

  pthread_create(&threadsPool[i].idThread, NULL, &treat, (void *)i);
  return; /* threadul principal returneaza */
}

int checkForErrors(int exception, const char *message)
{
  if (exception == SERVER_ERROR)
  {
    perror(message);
    exit(1);
  }
  return exception;
}

void *treat(void *arg)
{
  int client;

  struct sockaddr_in from;
  bzero(&from, sizeof(from));
  printf("[thread]- %d - pornit...\n", (int)arg);
  fflush(stdout);

  for (;;)
  {
    int length = sizeof(from);
    pthread_mutex_lock(&mlock);
    //printf("Thread %d trezit\n",(int)arg);
    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[thread]Eroare la accept().\n");
    }
    pthread_mutex_unlock(&mlock);
    threadsPool[(int)arg].thCount++;

    raspunde(client, (int)arg); //procesarea cererii
    /* am terminat cu acest client, inchidem conexiunea */
    close(client);
  }
}

void handle_request(const int clientSocket, char *request, int idThread)
{

  char response[BUFFERSIZE];
  bzero(response, BUFFERSIZE);


  if (!strcmp(request, "/help"))
  {
    strcat(response, "\t Help Section\r\n");
    strcat(response, "<< /quit\tQuit app\r\n");
    strcat(response, "<< /vote <id>\tVote a specific melody\r\n");
    strcat(response, "<< /category <name>\tGet the top for a category\r\n");
    strcat(response, "<< /login\tLog in the app\r\n");
    strcat(response, "<< /register\tRegister an account\r\n");
    strcat(response, "<< /help\tShow help\r\n");
  }
  else if (!strcmp(request, "/register"))
  {
  }
  else if (!strcmp(request, "/login"))
  {
  }
  else if (!strcmp(request, "/vote"))
  {
  }
  else if (!strcmp(request, "/category"))
  {
  }
  else if (!strcmp(request, "/list"))
  {
  }
  else if (!strcmp(request, "/quit"))
  {
  }

  printf("[Thread %d]Trimitem mesajul inapoi...%s\n", idThread, response);

  // strcpy(response,request);
  /* returnam mesajul clientului */
  checkForErrors(write(clientSocket, &response, sizeof(response)), "[Thread]Eroare la write() catre client.\n");
  bzero(response, BUFFERSIZE);
  printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", idThread);
}

void raspunde(int cl, int idThread)
{
  char request[BUFFERSIZE]; //mesajul primit de trimis la client

  while (1)
  {

    checkForErrors(read(cl, &request, BUFFERSIZE), "[Thead] Eroare la read() de la client\n");

    printf("[Thread %d]Mesajul a fost receptionat...%s\n", idThread, request);
    request[strlen(request)] = '\0';

    if (!strcmp(request, "/quit"))
    {
      checkForErrors(write(cl, &request, sizeof(request)), "[Thread]Eroare la write() quit catre client.\n");

      break;
    }

    handle_request(cl, request, idThread);
    bzero(request,BUFFERSIZE);
  }
  printf("[Thread %d]Am incheiat conexiunea cu clientul.\n", idThread);
}