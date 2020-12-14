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
#include <assert.h>
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

typedef struct 
{
  int userID;
  char *username;
  int isAdmin;
  int canVote;
}user;

Thread *threadsPool; //un array de structuri Thread

char response[BUFFERSIZE];
sqlite3 *db;
sqlite3_stmt *sqlStatment;
int dbConnection;
char *error_message;
int sd;                                            //descriptorul de socket de ascultare
int nthreads;                                      //numarul de threaduri
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER; // variabila mutex ce va fi partajata de threaduri


char* login(char* request, user *me);
void raspunde(int cl, int idThread, user *me);
void prepareDBConnection()
{
  dbConnection = sqlite3_open("rc.db", &db);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);

    return 1;
  }
}

char *registerUser(char *request)
{

  char username[257];
  char password[255];
  char response[BUFFERSIZE];
  printf("[server] Request:%s\n", request);

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ,.-");

  if (pointer != NULL)
  {
    strcpy(username, pointer);
    pointer = strtok(NULL, " ,.-");
    printf("Username:%s\n",username);
  }
  else
  {
    return "Trebuie sa introduci un unsername\n ex: /reister [username] [passowrd]";
  }

  if (pointer != NULL)
  {
    strcpy(password, pointer);
    pointer = strtok(NULL, " ,.-");
    printf("Passwrod:%s\n",password);
  }
  else
  {
    return "Trebuie sa introduci o parola\n ex: /reister [username] [passowrd]";
  }

  char *sqlQuery = "INSERT INTO users(username,password,isAdmin) VALUES(?,?,?)";
 
  dbConnection = sqlite3_prepare_v2(db, sqlQuery, -1, &sqlStatment, 0);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);

    return "Internal server error";
  }

  sqlite3_bind_text(sqlStatment, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text(sqlStatment, 2, password, -1, SQLITE_STATIC);
  sqlite3_bind_int(sqlStatment, 3, 0);

  dbConnection = sqlite3_step(sqlStatment); 
  if (dbConnection == SQLITE_DONE)
  {
    sqlite3_finalize(sqlStatment);
  }
  else
  {
    fprintf(stderr, "Failed to registering the user\n");
    fprintf(stderr, "SQL error: %s\n", error_message);
    sqlite3_free(error_message);
    return "Internal Server Error";
  }

  return "You have been registerd, please log in.";
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
  user me;
  struct sockaddr_in from;
  bzero(&from, sizeof(from));
  printf("[thread]- %d - pornit...\n", (int)arg);
  fflush(stdout);
  

  for (;;)
  {
    me.isAdmin = 0;
    me.canVote = 1;
    me.userID = NULL;
    me.username = NULL;
    int length = sizeof(from);
    pthread_mutex_lock(&mlock);
    //printf("Thread %d trezit\n",(int)arg);
    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[thread]Eroare la accept().\n");
    }
    pthread_mutex_unlock(&mlock);
    threadsPool[(int)arg].thCount++;
   
    raspunde(client, (int)arg, &me); //procesarea cererii
    /* am terminat cu acest client, inchidem conexiunea */
    close(client);
  }
  sqlite3_close(db);
}

int treatRow(void *NotUsed, int argc, char **argv,
             char **azColName)
{

  NotUsed = 0;
  char *aux;
  for (int i = 0; i < argc; i++)
  {
    strcat(response, argv[i]);
    strcat(response, " ");
  }
  strcat(response, "\n");
  printf("\n");

  return 0;
}

void getTop()
{

  char *sql = "SELECT id,title,nr_voturi FROM melodies ORDER BY nr_voturi DESC";
  bzero(response, BUFFERSIZE);
  dbConnection = sqlite3_exec(db, sql, treatRow, 0, &error_message);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Failed to select data\n");
    fprintf(stderr, "SQL error: %s\n", error_message);

    sqlite3_free(error_message);
    // sqlite3_close(db);

    bzero(response, BUFFERSIZE);
    strcat(response, "500 Internal server error\n");
  }
}


void addMelody(char *request){
  char melody[255];
  char category[255];
  char yt_link[255];
  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ,.-");

  if (pointer != NULL)
  {
    strcpy(melody, pointer);
    pointer = strtok(NULL, " ,.-");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /add [title] [yt_link] [categoryID]");
    return;
  }

  if (pointer != NULL)
  {
    strcpy(yt_link, pointer);
    pointer = strtok(NULL, " ,.-");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /add [title] [yt_link] [categoryID]");
    return;
  }

  if (pointer != NULL)
  {
    strcpy(category, pointer);
    pointer = strtok(NULL, " ,.-");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /add [title] [yt_link] [categoryID]");
    return;
  }

  char *sqlQuery = "INSERT INTO melodies VALUES(?,?)";

  dbConnection = sqlite3_prepare_v2(db, sqlQuery, -1, &sqlStatment, 0);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);

    return "Internal server error";
  }

  sqlite3_bind_text(sqlStatment, 1, melody, -1, SQLITE_STATIC);
  sqlite3_bind_text(sqlStatment, 2, yt_link, -1, SQLITE_STATIC);

  dbConnection = sqlite3_step(sqlStatment); 
  if (dbConnection == SQLITE_DONE)
  {
    sqlite3_finalize(sqlStatment);
  }
  else
  {
    fprintf(stderr, "Failed to registering the user\n");
    fprintf(stderr, "SQL error: %s\n", error_message);
    sqlite3_free(error_message);
    return "Internal Server Error";
  }

  return "You have been registerd, please log in.";
}

void getCategories(char* request)
{

  char category[255];
  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ,.-");

  if (pointer != NULL)
  {
    strcpy(category, pointer);
    pointer = strtok(NULL, " ,.-");
  }
  else
  {
    strcat(response,"Trebuie sa introduci o categorie\n /category [name]");
    return;
  }

  printf("[server] Am citit categoria: %s\n", category);

  char *sql = "SELECT m.id,m.title,m.nr_voturi FROM melodies m JOIN rmcat r ON r.id_melody=m.id JOIN categories c ON c.id=r.id_category WHERE c.name=? ORDER BY m.nr_voturi DESC";

  dbConnection = sqlite3_prepare_v2(db, sql, -1, &sqlStatment, 0);

  if (dbConnection == SQLITE_OK)
  {

    sqlite3_bind_text16(sqlStatment, 0, category, -1, SQLITE_STATIC);
  }
  else
  {

    fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(sqlStatment);
    sqlite3_free(error_message);
    sqlite3_close(db);
    bzero(response, BUFFERSIZE);
    strcat(response, "500 Internal server error\n");
    return;
  }

  bzero(response, BUFFERSIZE);
  dbConnection = sqlite3_step(dbConnection);
  while ((dbConnection = sqlite3_step(dbConnection)) == SQLITE_ROW)
  {
    // strcat(response, sqlite3_column_text(dbConnection, i));
    printf("%s\n",sqlite3_column_text(dbConnection, 0));
    
  }

  // printf("[server]: Rapunsul este:%s \n", response);

  if (strlen(response) == 0)
  {
    strcpy(response, "Nu exista melodii pentru aceasta categorie");
  }

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Failed to select data\n");
    fprintf(stderr, "SQL error: %s\n", error_message);
    sqlite3_finalize(sqlStatment);
    sqlite3_free(error_message);
    sqlite3_close(db);
    bzero(response, BUFFERSIZE);
    strcat(response, "500 Internal server error\n");
    return;
  }
  sqlite3_finalize(sqlStatment);
}

void handle_request(const int clientSocket, char *request, int idThread, user *me)
{
  bzero(response, BUFFERSIZE);

  if (strstr(request, "/help"))
  {
    strcat(response, "\t Help Section\r\n");
    strcat(response, "<< /quit\tQuit app\r\n");
    strcat(response, "<< /vote <id>\tVote a specific melody\r\n");
    strcat(response, "<< /category <name>\tGet the top for a category\r\n");
    strcat(response, "<< /login <username> <password>\tLog in the app\r\n");
    strcat(response, "<< /register\tRegister an account\r\n");
    strcat(response, "<< /help <username> <password>\tShow help\r\n");
    if(me->isAdmin){
      strcat(response, "<< /remove\t Remove a melody\r\n");
      strcat(response, "<< /banvote\t Ban a user from voting\r\n");
    }
  }
  else if (strstr(request, "/register"))
  {
    strcat(response, registerUser(request));
  }
  else if (strstr(request, "/login"))
  {
    if(me->username != NULL){
      strcat(response,"Sunteti deja inregistrat\n");
    }else{
      strcat(response, login(request, me));
    }
  }
  else if (strstr(request, "/vote"))
  {
    if(me->username != NULL){
     strcat(response, "Ai votat melodia");
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/category"))
  {
    //strcat(response,"Aici ai categoriile existente");//
    if(me->username != NULL){
      getCategories(request);
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/top"))
  {
    getTop();
  }
  else if (strstr(request, "/add"))
  {
    if(me->username != NULL){
    strcat(response, "Ai adaugat melodia");
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/remove"))
  {
    if(me->username != NULL){
      strcat(response, "Ai sters melodia");
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/whoami"))
  {
    if(me->username != NULL)
      strcat(response, me->username);
    
    strcat(response, "Nu sunteti inregistrat");
  }
  else
  {
    strcat(response, "Comanda Inexistenta!");
  }

  printf("[Thread %d]Trimitem mesajul inapoi...%s\n", idThread, response);

  // strcpy(response,request);
  /* returnam mesajul clientului */
  checkForErrors(write(clientSocket, &response, sizeof(response)), "[Thread]Eroare la write() catre client.\n");
  bzero(response, BUFFERSIZE);
  printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", idThread);
}

void raspunde(int cl, int idThread, user *me)
{
  char request[BUFFERSIZE]; //mesajul primit de trimis la client

  while (1)
  {

    checkForErrors(read(cl, &request, BUFFERSIZE), "[Thead] Eroare la read() de la client\n");

    request[strlen(request)] = '\0';
    printf("[Thread %d]Mesajul a fost receptionat...%s\n", idThread, request);
    if (strlen(request) == 0)
      break;
    if (!strcmp(request, "/quit"))
    {
      checkForErrors(write(cl, &request, sizeof(request)), "[Thread]Eroare la write() quit catre client.\n");

      break;
    }

    handle_request(cl, request, idThread, me);
    bzero(request, BUFFERSIZE);
  }
  printf("[Thread %d]Am incheiat conexiunea cu clientul.\n", idThread);
}

char* login(char* request, user *me) {
  char username[255];
  char password[255];
  char response[BUFFERSIZE];
  printf("[server] Request:%s\n", request);

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ,.-");

  if (pointer != NULL)
  {
    strcpy(username, pointer);
    pointer = strtok(NULL, " ,.-");
    printf("Username:%s\n",username);
  }
  else
  {
    return "Trebuie sa introduci un unsername\n ex: /login [username] [passowrd]";
  }

  if (pointer != NULL)
  {
    strcpy(password, pointer);
    pointer = strtok(NULL, " ,.-");
    printf("Passwrod:%s\n",password);
  }
  else
  {
    return "Trebuie sa introduci o parola\n ex: /login [username] [passowrd]";
  }

  char* sqlQuery ="SELECT id, username, isAdmin, canVote FROM users WHERE username=? AND password=?";


  dbConnection = sqlite3_prepare_v2(db, sqlQuery, -1, &sqlStatment, 0);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);

    return "Internal server error";
  }

  sqlite3_bind_text(sqlStatment, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text(sqlStatment, 2, password, -1, SQLITE_STATIC);

  
  // dbConnection = sqlite3_exec(db, sqlQuery, populateUserData, 0, &error_message);
  dbConnection = sqlite3_step(sqlStatment);
  if (dbConnection != SQLITE_ROW)
  {
    fprintf(stderr, "Failed to select data\n");
    fprintf(stderr, "SQL error: %s\n", error_message);

    sqlite3_free(error_message);
    // sqlite3_close(db);

    bzero(response, BUFFERSIZE);
   
    return "Internal server error";
  }


  me->userID = sqlite3_column_int(sqlStatment,0);
  me->username  = username;
  me->isAdmin = sqlite3_column_int(sqlStatment,2);
  me->canVote = sqlite3_column_int(sqlStatment,3);


  printf("username:%s\n",me->username);
  printf("userid:%d\n",me->userID);
  printf("isAdmin:%d\n",me->isAdmin);
  printf("canVote:%d\n",me->canVote);

  return "You are logged in.";
}