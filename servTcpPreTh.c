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
#include <sqlite3.h>

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

char response[BUFFERSIZE];
sqlite3 *db;
sqlite3_stmt *sqlStatment;
int dbConnection; 
char *error_message;
int sd;                                            //descriptorul de socket de ascultare
int nthreads;                                      //numarul de threaduri
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER; // variabila mutex ce va fi partajata de threaduri

struct user{
  char *username;
  int isAdmin;
}me; 


void raspunde(int cl, int idThread);
void prepareDBConnection(){
   dbConnection = sqlite3_open("rc.db", &db);


   if (dbConnection != SQLITE_OK) {
        
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        
        return 1;
    }
}


char* registerUser(int clientSocket){

  char username[257];
  char password[255];
  char response[BUFFERSIZE];

  bzero(response, BUFFERSIZE);
  strcat(response, "Username");
  printf("Scriu username\n");
  checkForErrors(write(clientSocket, &response, sizeof(response)), "[Thread]Eroare la write() catre client.\n");
  bzero(response, BUFFERSIZE);
  printf("Citesc username\n");
  checkForErrors(read(clientSocket, &username, 256), "[Thead] Eroare la read() de la client\n");
    
    // printf("[Thread %d]Mesajul a fost receptionat...%s\n", idThread, request);
  username[strlen(username)] = '\0';
  bzero(response, BUFFERSIZE);
  printf("Username:%s\n", username);

  bzero(response, BUFFERSIZE);
  strcat(response, "Password:");  
  checkForErrors(write(clientSocket, &response, sizeof(response)), "[Thread]Eroare la write() catre client.\n");
  bzero(response, BUFFERSIZE);

 while(strlen(password) == 0){
  bzero(password, BUFFERSIZE);
  checkForErrors(read(clientSocket, &password, 256), "[Thead] Eroare la read() de la client\n");
  password[strlen(password)] = '\0';
 }  
  printf("Password:%s\n",password);

  return "Register";
  // dbConnection = sqlite3_prepare_v2(db, "INSERT INTO users(username,password,isAdmin) VALUES(?,?,?)", -1, &sqlStatment, 0);

  //   if (dbConnection != SQLITE_OK) {
        
  //       fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
  //       sqlite3_close(db);
        
  //       return 1;
  //   }

  //   dbConnection = sqlite3_step(sqlStatment);


  //   if(dbConnection == SQLITE_ROW){
  //     printf("%s\n", sqlite3_column_text(sqlStatment,0));
  //   }

  //   sqlite3_finalize(sqlStatment);
}

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

  me.isAdmin = 0;
  me.username = NULL; 
  prepareDBConnection();

  /* servim in mod concurent clientii...folosind thread-uri */
  for (;;)
  {
    printf("[server]Asteptam la portul %d...\n", PORT);
    pause();
  }

  sqlite3_close(db);
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
  sqlite3_close(db);  
}


int treatRow(void *NotUsed, int argc, char **argv, 
                    char **azColName) {
    
    NotUsed = 0;
    char *aux;
    for (int i = 0; i < argc; i++) {    
        strcat(response,argv[i]);
        strcat(response," ");
        
    }
    strcat(response,"\n");
    printf("\n");
    
    return 0; 
}

void getTop(){

    char *sql = "SELECT id,title,nr_voturi FROM melodies ORDER BY nr_voturi DESC";
    bzero(response,BUFFERSIZE);
     dbConnection = sqlite3_exec(db, sql, treatRow, 0, &error_message);

    if (dbConnection != SQLITE_OK ) {
        
        fprintf(stderr, "Failed to select data\n");
        fprintf(stderr, "SQL error: %s\n", error_message);

        sqlite3_free(error_message);
        sqlite3_close(db);
        
        return "500 Internal server error\n";
    } 
}


char* getCategories(int clientSocket)
{

    char* category[255];


    checkForErrors(write(clientSocket, "Ce categorie te intereseaza?", 255), "[Thread]Eroare la write() catre client.\n");

    checkForErrors(read(clientSocket, &category, 256), "[Thead] Eroare la read() de la client\n");


    char *sql = "SELECT id,title,nr_voturi FROM melodies WHERE category=? ORDER BY nr_voturi DESC";
    
  //  dbConnection = sqlite3_exec(db, sql, treatRow, 0, &error_message);
    dbConnection = sqlite3_prepare_v2(db, sql, -1, &sqlStatment, 0);
    
    if (dbConnection == SQLITE_OK) {
        
        sqlite3_bind_text(sqlStatment, 1, category,-1,0);
    } else {
        
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    }

    bzero(response,BUFFERSIZE);
    int i = 0;
    while(sqlite3_step(dbConnection) == SQLITE_ROW){
      strcat(response,sqlite3_column_text(dbConnection, i));
      ++i;
    }
    


    if (dbConnection != SQLITE_OK ) {
        
        fprintf(stderr, "Failed to select data\n");
        fprintf(stderr, "SQL error: %s\n", error_message);

        sqlite3_free(error_message);
        sqlite3_close(db);
        
        return "500 Internal server error\n";
    } 
    sqlite3_finalize(sqlStatment);
    return response;
}

void handle_request(const int clientSocket, char *request, int idThread)
{
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
    strcat(response, registerUser(clientSocket));
  }
  else if (!strcmp(request, "/login"))
  {
    strcat(response,"Te-ai logat cu success");
  }
  else if (!strcmp(request, "/vote"))
  {
    strcat(response,"Ai votat melodia");
  }
  else if (!strcmp(request, "/category"))
  {
    //strcat(response,"Aici ai categoriile existente");//
    strcat(response, getCategories(clientSocket));
  }
  else if (!strcmp(request, "/top"))
  {
    getTop();
  }
  else if (!strcmp(request, "/add"))
  {
    strcat(response,"Ai adaugat melodia");
  }else{
    strcat(response,"Comanda Inexistenta!");
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
    
   
    request[strlen(request)] = '\0';
    printf("[Thread %d]Mesajul a fost receptionat...%s\n", idThread, request);
    if(strlen(request) == 0) break;
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
