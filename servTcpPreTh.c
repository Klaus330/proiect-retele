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
#include <assert.h>
#include <regex.h> 
#include <crypt.h>
#include <unistd.h>
#include <time.h>
#include <sqlite3.h>
#include <stdio.h>
/* portul folosit */
#define PORT 2909
#define BUFFERSIZE 4096
#define SERVER_ERROR -1
#define PRIMARY_KEY_ERROR 19
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
  int loginTries;
}user;

Thread *threadsPool; //un array de structuri Thread

char response[BUFFERSIZE];
int sd;                                            //descriptorul de socket de ascultare
int nthreads;                                      //numarul de threaduri
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER; // variabila mutex ce va fi partajata de threaduri
int nrActiveThreads = 0;


sqlite3 *db;
sqlite3_stmt *sqlStatment;
int dbConnection;
char *error_message;

int prepareQuery(char *sqlQuery);
int checkIfQueryDone(int code);
// Functions
char* login(char* request, user *me);
void raspunde(int cl, int idThread, user *me);
char *registerUser(char *request);
int checkForErrors(int exception, const char *message);
void getTop();
void getCategories(char *request);
void handle_request(const int clientSocket, char *request, int idThread, user *me);
void addMelody(char *request);
void vote(char *request,user *me);
void postComment(char *request, user *me);
void getComments(char *request);
void banvote(char *request);
int validateRegEx(char *regex, char *str);
void expandThreadPool();
void contractThreadPool();


void prepareDBConnection()
{
  dbConnection = sqlite3_open("rc.db", &db);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    exit(-1);
  }
  
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
    while(1){
     if(nrActiveThreads == nthreads){
       expandThreadPool();
     }
    }
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
    me.loginTries = 0;
    int length = sizeof(from);
    pthread_mutex_lock(&mlock);
    //printf("Thread %d trezit\n",(int)arg);
    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[thread]Eroare la accept().\n");
    }
    nrActiveThreads++;
    pthread_mutex_unlock(&mlock);
    threadsPool[(int)arg].thCount++;
   
    raspunde(client, (int)arg, &me); //procesarea cererii
    /* am terminat cu acest client, inchidem conexiunea */
    close(client);
    
    if(nrActiveThreads != nthreads && nthreads > 1){
       contractThreadPool();
    }
    nrActiveThreads--;
  }
  sqlite3_close(db);
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

void handle_request(const int clientSocket, char *request, int idThread, user *me)
{
  bzero(response, BUFFERSIZE);
  if (strstr(request, "/help"))
  {
    strcat(response, "\t Help Section\r\n");
    strcat(response, "<< /quit\tQuit app\r\n");
    strcat(response, "<< /login <username> <password>\tLog in the app\r\n");
    strcat(response, "<< /register <username> <password>\tRegister an account\r\n");
    strcat(response, "<< /help\tShow help\r\n");
    if(me->username != NULL){
      strcat(response, "<< /top <id>\tShow the general Top 30 meldies\r\n");
      strcat(response, "<< /whoami\tWho am I?\r\n");
      strcat(response, "<< /vote <id>\tVote a specific melody\r\n");
      strcat(response, "<< /category <name>\tGet the top for a category\r\n");
      strcat(response, "<< /add <name>#<yt_link>#<categoryId>\tAdd a melody to the top\r\n");
      strcat(response, "<< /comment <body>#<melodyId>\tComment at a melody\r\n");
      strcat(response, "<< /showcomments <melodyId>\tComment at a melody\r\n");
    }
    if(me->isAdmin){
      strcat(response, "<< /remove <melodyId>\t Remove a melody\r\n");
      strcat(response, "<< /banvote <username>\t Ban a user from voting\r\n");
    }
  }
  else if (strstr(request, "/register"))
  {
    
    if(me->username != NULL){
      strcat(response,"Sunteti deja inregistrat\n");
    }else{
      strcat(response, registerUser(request));
    }
  }
  else if (strstr(request, "/login"))
  {
    if(me->username == NULL){
      strcat(response, login(request, me));
      char incercari[200];
      sprintf(incercari, "\n\e[1;33mNumar de incercari ramase:%d\e[0m", 3-me->loginTries);
      strcat(response,incercari);
      if(me->loginTries>=3){
        bzero(response,BUFFERSIZE);
        strcat(response,"\e[1;31mPrea multe incercari\e[0m!\n");
        strcat(response,"/quit");
      }
    }else{
      strcat(response, "Esti deja logat\n");
    }
  }
  else if (strstr(request, "/vote"))
  {
    if(me->username != NULL){
     if(me->canVote){
      vote(request,me);
     }else{
      strcat(response,"Nu mai ai dreptul de a vota!\n"); 
     }
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/category"))
  {
    if(me->username != NULL){
      getCategories(request);
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/top"))
  {
    if(me->username != NULL){
      getTop();
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/add"))
  {	  
    if(me->username != NULL){
      addMelody(request);
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }
  else if (strstr(request, "/remove"))
  {
    if(me->username != NULL){
      deleteMelody(request);
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }else if (strstr(request, "/banvote"))
  {
    if(me->username != NULL){
      banvote(request);
    }else{
      strcat(response,"Nu sunteti inregistrat\n");
    }
  }	  	  
  else if (strstr(request, "/whoami"))
  {
    if(me->username != NULL){
      strcat(response, me->username);
    }else{
      strcat(response, "Nu sunteti inregistrat");
    }
  }
  else if (strstr(request, "/comment"))
  {
    if(me->username != NULL){
      postComment(request,me);
    }else{
      strcat(response, "Nu sunteti inregistrat");
    }
  }
  else if (strstr(request, "/showcomments"))
  {
    if(me->username != NULL){
      getComments(request);
    }else{
      strcat(response, "Nu sunteti inregistrat");
    }
  }
  else
  {
    strcat(response, "Comanda Inexistenta!");
  }

  printf("[Thread %d]Trimitem mesajul inapoi...%s\n", idThread, response);

  // strcpy(response,request);
  /* returnam mesajul clientului */
  checkForErrors(write(clientSocket, &response, sizeof(response)), "[Thread]Eroare la write() catre client.\n");
  printf("[Thread %d]Mesajul a fost trasmis cu succes.%s\n", idThread, response);
  bzero(response, BUFFERSIZE);
}


char *registerUser(char *request)
{

  char username[257];
  char password[255];
  char response[BUFFERSIZE];
  printf("[server] Request:%s\n", request);

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    strcpy(username, pointer);
    pointer = strtok(NULL, " ");
    printf("Username:%s\n",username);
  }
  else
  {
    return "Trebuie sa introduci un unsername\n ex: /reister [username] [passowrd]";
  }

  if (pointer != NULL)
  {
    strcpy(password, crypt(pointer,"k7"));
    pointer = strtok(NULL, " ");
  }
  else
  {
    return "Trebuie sa introduci o parola\n ex: /reister [username] [passowrd]";
  }

  char *sqlQuery = "INSERT INTO users(username,password,isAdmin) VALUES(?,?,?)";
 
  checkForErrors(prepareQuery(sqlQuery),"[category]Could not prepare the SQL Query!\n");

  sqlite3_bind_text(sqlStatment, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text(sqlStatment, 2, password, -1, SQLITE_STATIC);
  sqlite3_bind_int(sqlStatment, 3, 0);

  dbConnection = sqlite3_step(sqlStatment); 
  checkForErrors(checkIfQueryDone(dbConnection), "[login]Error at finishing the query!\n");
  return "You have been registerd, please log in.";
}

char* login(char* request, user *me) {
  char username[255];
  char password[255];
  char response[BUFFERSIZE];
  printf("[server] Request:%s\n", request);

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    strcpy(username, pointer);
    pointer = strtok(NULL, " ");
  }
  else
  {
    me->loginTries = me->loginTries + 1;
    return "Trebuie sa introduci un unsername\n ex: /login [username] [passowrd]";
  }

  if (pointer != NULL)
  {
    strcpy(password, crypt(pointer,"k7"));
    pointer = strtok(NULL, " ");
  }
  else
  {
    me->loginTries = me->loginTries + 1;
    return "Trebuie sa introduci o parola\n ex: /login [username] [passowrd]";
  }

  char* sqlQuery ="SELECT id, username, isAdmin, canVote FROM users WHERE username=? AND password=?";


   checkForErrors(prepareQuery(sqlQuery),"[login]Could not prepare the SQL Query!\n");

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
    me->loginTries = me->loginTries + 1;
    return "Input invalid. Please check again your credentials.";
  }


  me->userID = sqlite3_column_int(sqlStatment,0);
  me->username  = username;
  me->isAdmin = sqlite3_column_int(sqlStatment,2);
  me->canVote = sqlite3_column_int(sqlStatment,3);
  
  return "You are logged in.";
}


void getCategories(char *request)
{

  char *category[255];

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    strcpy(category, pointer);
    pointer = strtok(NULL, " ");
    printf("Categoria:%s %d\n",category,strlen(category));
  }
  else
  {
    return "Trebuie sa introduci o categorie\n ex: /category [categoryName]";
  }


  char *sql = "SELECT id FROM categories WHERE name=?";
  checkForErrors(prepareQuery(sql),"[category]Could not prepare the SQL Query!\n");
  sqlite3_bind_text(sqlStatment, 1, category, -1, SQLITE_STATIC);
  dbConnection = sqlite3_step(sqlStatment);
  int categoryId = sqlite3_column_int(sqlStatment,0);
   
  char *sqlQuery = "SELECT m.id,m.title,m.nr_voturi, m.yt_link FROM melodies m JOIN rmcat r ON r.id_melody=m.id JOIN categories c ON c.id=r.id_category WHERE c.id=? ORDER BY m.nr_voturi DESC";

  checkForErrors(prepareQuery(sqlQuery),"[category]Could not prepare the SQL Query!\n");
  sqlite3_bind_int(sqlStatment, 1, categoryId);
  bzero(response, BUFFERSIZE);
  int i = 0;
  dbConnection = sqlite3_step(sqlStatment);
  strcat(response, "\n\t Meldiile din categria aleasa \n");
  while (dbConnection == SQLITE_ROW)
  {
    
      for(int i=0; i<4; i++){
        printf("%s", sqlite3_column_text(sqlStatment, i));
        strcat(response, sqlite3_column_text(sqlStatment, i));
        if(i==0){
          strcat(response, ".");  
        }
        strcat(response, " ");
        if(i==1){
          strcat(response, "-> ");
        }

        if(i==2){
          if(strcmp(sqlite3_column_text(sqlStatment, i),"1")==0){
            strcat(response, "vot -> ");
          }else{
            strcat(response, "voturi -> ");
          }
        }
      }
      printf("\n");
      strcat(response, "\n");
     dbConnection = sqlite3_step(sqlStatment);
  }

  if (strlen(response) == 0)
  {
    strcat(response, "Nu exista melodii pentru aceasta categorie");
    sqlite3_finalize(sqlStatment);
    return;
  }
}


int treatRow(void *NotUsed, int argc, char **argv,
             char **azColName)
{

  NotUsed = 0;
  char *aux;
  for (int i = 0; i < argc; i++)
  {
    strcat(response, argv[i]);
    if(i==0){
      strcat(response, ".");  
    }
  
    strcat(response, " ");
    if(i==1){
      strcat(response, "-> ");
    }

    if(i==2){
      if(strcmp(argv[i],"1")==0){
        strcat(response, "vot -> ");
      }else{
        strcat(response, "voturi -> ");
      }
    }
  }
  strcat(response, "\n");
  printf("\n");

  return 0;
}

void getTop()
{

  char *sql = "SELECT id,title,nr_voturi, yt_link FROM melodies ORDER BY nr_voturi DESC";
  bzero(response, BUFFERSIZE);
  strcat(response, "\n\t Top 30 melodii \n \t (ID TITLE VOTE_NR)\n");
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

void validateYtLink(char *link){
   switch(validateRegEx("(http:|https:)?\\/\\/(www\\.)?(youtube.com|youtu.be)\\/(watch)?(\\?v=)?(\\\S+)?", link)){
      case 1:
      break;
      case 0:
        strcat(response,"Linkul nu este valid");
        return;
      break;
      case -1:
        strcat(response,"Internal Server Error");
        return;
      break;  
      case -2:
        strcat(response,"Could not compile");
        return;
      break;      
    }
}

void addMelody(char *request){
  char melody[255];
  int categoryId;
  char yt_link[255];
  char *pointer;
  
  int i=5, j=0;
  while(request[i]!='#'){
    melody[j++]=request[i++];
  }
  printf("%s\n",melody);
  pointer = strtok(request, "#");
  pointer = strtok(NULL, "#");

  if (pointer != NULL)
  {
    strcpy(yt_link, pointer);
    pointer = strtok(NULL, "#");
    validateYtLink(yt_link);
  }
  else
  {
    strcat(response,"Invalid format. Ex: /add [title] [yt_link] [categoryID]");
    return;
  }

  if (pointer != NULL)
  {    
    categoryId = atoi(pointer);
    pointer = strtok(NULL, "#");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /add [title] [yt_link] [categoryID]");
    return;
  }

  char *sqlQuery = "INSERT INTO melodies(title,yt_link) VALUES(?,?)";

  checkForErrors(prepareQuery(sqlQuery),"[add]Could not prepare the SQL Query!\n");

  sqlite3_bind_text(sqlStatment, 1, melody, -1, SQLITE_STATIC);
  sqlite3_bind_text(sqlStatment, 2, yt_link, -1, SQLITE_STATIC);

  dbConnection = sqlite3_step(sqlStatment); 
  checkForErrors(checkIfQueryDone(dbConnection), "[add]Error at finishing the query!\n");

  int melodyId=0;
  char *sql = "SELECT id FROM melodies ORDER BY id DESC LIMIT 1";
  bzero(response, BUFFERSIZE);
  checkForErrors(prepareQuery(sql),"[add]Could not prepare the SQL Query!\n");
  
  dbConnection = sqlite3_step(sqlStatment); 
  if (dbConnection == SQLITE_ROW)
  {
    melodyId = sqlite3_column_int(sqlStatment,0);
    printf("melodyID:%d\n",melodyId);
  }else
  {
    fprintf(stderr, "Failed to select data\n");
    fprintf(stderr, "SQL error: %s\n", error_message);

    sqlite3_free(error_message);
    sqlite3_finalize(sqlStatment);
    bzero(response, BUFFERSIZE);
    strcat(response,"Internal server error");
    return;
  }

  
  char *sqlQ = "INSERT INTO rmcat VALUES(?,?)";

  checkForErrors(prepareQuery(sqlQ),"[add]Could not prepare the SQL Query!\n");
  sqlite3_bind_int(sqlStatment, 1, melodyId);
  sqlite3_bind_int(sqlStatment, 2, categoryId);
  

  dbConnection = sqlite3_step(sqlStatment); 
  checkForErrors(checkIfQueryDone(dbConnection), "[add]Error at finishing the query!\n");

  strcat(response,"You've added a new melody");
}


void vote(char *request, user *me){
  int melodyId;

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    melodyId = atoi(pointer);
    pointer = strtok(NULL, " ");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /vote [melodyId]");
    return;
  }

  // Check to see if there is a melody with the provided id
  char *sqlQuery = "SELECT id FROM melodies WHERE id=?";

  checkForErrors(prepareQuery(sqlQuery),"[vote]Could not prepare the SQL Query!\n");
  sqlite3_bind_int(sqlStatment, 1, melodyId);
  
  dbConnection = sqlite3_step(sqlStatment); 
  if(dbConnection == SQLITE_DONE){
    strcat(response,"Nu am gasit o melodie cu acest id!");
    return;
  }

  // Add the vote relationship to the votes table
  char *sql = "INSERT INTO votes VALUES(?,?)";

  checkForErrors(prepareQuery(sql),"[vote]Could not prepare the SQL Query!\n");

  sqlite3_bind_int(sqlStatment, 1, melodyId);
  sqlite3_bind_int(sqlStatment, 2, me->userID);
  
  dbConnection = sqlite3_step(sqlStatment); 
  
  if(dbConnection == PRIMARY_KEY_ERROR){
    strcat(response,"NU poti vota o melodie de doua ori!");
    return;
  }
  checkForErrors(checkIfQueryDone(dbConnection), "[vote]Error at finishing the query2!\n");

  // Add the new vote to the melody
  char *sqlQ = "UPDATE melodies SET nr_voturi=nr_voturi+1 WHERE id=?";

  checkForErrors(prepareQuery(sqlQ),"[vote]Could not prepare the SQL Query!\n");

  sqlite3_bind_int(sqlStatment, 1, melodyId);

  dbConnection = sqlite3_step(sqlStatment); 
  checkForErrors(checkIfQueryDone(dbConnection), "[vote]Error at finishing the query3!\n");

  strcat(response,"Votul tau a fost inregistrat");
}

void deleteMelody(char *request){
  int melodyId;

  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    melodyId = atoi(pointer);
    pointer = strtok(NULL, " ");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /vote [melodyId]");
    return;
  }


  char *sqlQuery = "DELETE FROM melodies WHERE id=?";

  checkForErrors(prepareQuery(sqlQuery),"[deleteMelody]Could not prepare the SQL Query!\n");
  sqlite3_bind_int(sqlStatment, 1, melodyId);

  dbConnection = sqlite3_step(sqlStatment); 
  checkForErrors(checkIfQueryDone(dbConnection), "[deleteMelody]Error at finishing the query!\n");
  
  strcat(response,"Melodia a fost stearsa\n");
}


int getLastCommentID(){
  int commentID=0;
  char *sql = "SELECT id FROM comments ORDER BY id DESC LIMIT 1";
  bzero(response, BUFFERSIZE);
  checkForErrors(prepareQuery(sql),"[getLastCommentID]Could not prepare the SQL Query!\n");
  
  dbConnection = sqlite3_step(sqlStatment); 
  if (dbConnection == SQLITE_ROW)
  {
    commentID = sqlite3_column_int(sqlStatment,0);
    return commentID;
  }else
  {
    fprintf(stderr, "Failed to select data\n");
    fprintf(stderr, "SQL error: %s\n", error_message);

    sqlite3_free(error_message);
    sqlite3_finalize(sqlStatment);
    return -1;
  }
}

void postComment(char *request, user *me){
  int melodyId;
  char comment[255];
  char *pointer;

  int i=9,j=0;
  while(request[i] != '#'){
    comment[j++]=request[i];
    i++;
  }
  printf("[server] Comment:%s\n",comment);
  pointer = strtok(request, "#");
  pointer = strtok(NULL, "#");
  
  if (pointer != NULL)
  {
    printf("[server] %s\n",pointer);
    melodyId = atoi(pointer);
  }
  else
  {
    strcat(response,"Invalid format. Ex: /comment '<body>'#<melodyId>");
    return;
  }

  // Insert the comment in the DB
  char *sqlQ = "INSERT INTO comments(body,id_user) VALUES(?,?)";
  checkForErrors(prepareQuery(sqlQ),"[postComment]Could not prepare the SQL Query!\n");

  sqlite3_bind_text(sqlStatment, 1, comment, -1, SQLITE_STATIC);
  sqlite3_bind_int(sqlStatment, 2, me->userID);
  
  dbConnection = sqlite3_step(sqlStatment); 
  
  checkForErrors(checkIfQueryDone(dbConnection), "[postComment]Error at finishing the query!\n");

  // Get the last comment ID
  int commentID;
  if((commentID = getLastCommentID()) == -1){
    strcat(response,"Internal server error");
    return;
  }

  // Insert the relationship between melody and comment
  char *string = "INSERT INTO rmcom VALUES(?,?)";
  checkForErrors(prepareQuery(string),"[postComment]Could not prepare the SQL Query!\n");

  sqlite3_bind_int(sqlStatment, 1, melodyId);
  sqlite3_bind_int(sqlStatment, 2, commentID);
  
  dbConnection = sqlite3_step(sqlStatment); 
  checkForErrors(checkIfQueryDone(dbConnection), "[postComment]Error at finishing the query!\n");
  
  strcat(response,"Commentul tau a fost adaugat!\n");
}



int getComment(void *NotUsed, int argc, char **argv,
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

void getComments(char *request){
  int melodyId;
  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    melodyId = atoi(pointer);
    pointer = strtok(NULL, " ");
  }
  else
  {
    bzero(response,BUFFERSIZE);
    strcat(response,"Invalid format. Ex: /showcomments <melodyId>");
    return;
  }

  char *sql = "SELECT username,body FROM comments c JOIN users u ON c.id_user=u.id JOIN rmcom r ON r.id_melody=? AND r.id_comment=c.id";
  
  checkForErrors(prepareQuery(sql),"[getComments]Could not prepare the SQL Query!\n");
  sqlite3_bind_int(sqlStatment, 1, melodyId);
  

  dbConnection = sqlite3_step(sqlStatment); 
  bzero(response, BUFFERSIZE);
  strcat(response, "\n\t Comentariile de la melodia dorita \n");
  while (dbConnection == SQLITE_ROW)
  {
    strcat(response,sqlite3_column_text(sqlStatment,0));//username
    strcat(response,": ");
    strcat(response,sqlite3_column_text(sqlStatment,1));//comment body
    strcat(response,"\n");
    dbConnection = sqlite3_step(sqlStatment); 
  }


  if(strlen(response) == 0)
  {
    bzero(response,BUFFERSIZE);
    strcat(response,"Nu exista comentarii pentru aceasta melodie\n");
  }

   checkForErrors(checkIfQueryDone(dbConnection), "[getComments]Error at finishing the query!\n"); 
}

void banvote(char *request){
  char username[255];
  char *pointer;
  pointer = strtok(request, " ");
  pointer = strtok(NULL, " ");

  if (pointer != NULL)
  {
    strcpy(username,pointer);
    pointer = strtok(NULL, " ");
  }
  else
  {
    strcat(response,"Invalid format. Ex: /banvote <username>");
    return;
  }

  char *sql = "UPDATE users SET canVote=0 WHERE username=?";
  checkForErrors(prepareQuery(sql),"[getComments]Could not prepare the SQL Query!\n");
  sqlite3_bind_text(sqlStatment, 1, username, -1, SQLITE_STATIC);
  
  dbConnection = sqlite3_step(sqlStatment); 
  bzero(response, BUFFERSIZE);
  checkForErrors(checkIfQueryDone(dbConnection), "[getComments]Error at finishing the query!\n"); 

  strcat(response,"Dreptul utilizatorului de a vota a fost blocat!\n");
}


int validateRegEx(char *exp,char *string)
{
  regex_t regex;
  int reti;
  char error_message[100];
  printf("Linkul oferit: %s\n",string);
  /* Compile regular expression */
  reti = regcomp(&regex, exp, REG_EXTENDED);
  if (reti) {
      fprintf(stderr, "Could not compile regex\n");
      regfree(&regex);
      return -2;
  }

  /* Execute regular expression */
  reti = regexec(&regex, string, 0, NULL, 0);
  if (!reti) {
      regfree(&regex);
      return 1;
  }
  else if (reti == REG_NOMATCH) {
      regfree(&regex);
      return 0;
  }
  else {
      regerror(reti, &regex, error_message, sizeof(error_message));
      fprintf(stderr, "Regex match failed: %s\n", error_message);
    regfree(&regex);
      return -1;
  }
}


void expandThreadPool(){
  size_t myarray_size = nthreads;

  myarray_size += 1;
  Thread* newthreadsPool = realloc(threadsPool, myarray_size * sizeof(Thread));
  if (newthreadsPool) {
    threadsPool = newthreadsPool;
  } else {
  // deal with realloc failing because memory could not be allocated.
  }

  nthreads++;
  threadCreate(nthreads);
  printf("Am creat un nou thread!\n");
  
}

void contractThreadPool(){

  size_t myarray_size = nthreads;

  myarray_size -= 1;
  Thread* newthreadsPool = realloc(threadsPool, myarray_size * sizeof(Thread));
  if (newthreadsPool) {
    threadsPool = newthreadsPool;
  } else {
    
  }
  nthreads--;
}

int prepareQuery(char *sqlQuery){
  dbConnection = sqlite3_prepare_v2(db, sqlQuery, -1, &sqlStatment, 0);

  if (dbConnection != SQLITE_OK)
  {

    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
  }
  return 0;
}

int checkIfQueryDone(int code){
   if (code == SQLITE_DONE)
  {
    sqlite3_finalize(sqlStatment);
    return 0;
  }
  else
  {
    fprintf(stderr, "Failed to registering the user\n");
    fprintf(stderr, "SQL error: %s\n", error_message);
    sqlite3_free(error_message);
    return -1;
  }
}

