#include <sqlite3.h>
#include <stdio.h>

sqlite3 *db;
sqlite3_stmt *sqlStatment;
int dbConnection;
char *error_message;

int prepareQuery(char *sqlQuery);
int checkIfQueryDone(int code);