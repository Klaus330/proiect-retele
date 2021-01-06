#include "database.h"

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

