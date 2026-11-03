#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <errno.h>
#include <syslog.h>

#include "utils.h"
#include "cache.h"

static sqlite3* db = NULL;

/*
 * Constructor/Destructor when the library is loaded
 *
 * See: http://man7.org/linux/man-pages/man3/dlopen.3.html
 *
 */
__attribute__((constructor))
static void
init(void)
{
  D3("Initializing the ega library");
  (void)cache_open();
}

__attribute__((destructor))
static void
destroy(void)
{
  D3("Cleaning up the ega library");
  cache_close(); 
}

bool
cache_open(void)
{
  if( !loadconfig() ){ REPORT("Invalid configuration"); return false; }

  if(!options->use_cache) return false; /* no cache */

  if(db != NULL && sqlite3_errcode(db) == SQLITE_OK){ D3("Cache already opened"); return true; }

  D2("Opening cache");

  D1("Connection to: %s", options->db_path);
  sqlite3_open(options->db_path, &db); /* owned by the caller (usually root) and rw-r--r-- */
  if (db == NULL){ D1("Failed to allocate database handle"); return false; }
  D3("DB Connection: %p", db);
  
  if( sqlite3_errcode(db) != SQLITE_OK) {
    D1("Failed to open DB: [%d] %s", sqlite3_extended_errcode(db), sqlite3_errstr(sqlite3_extended_errcode(db)));
    return false;
  }
  
  /* create table */
  D2("Creating the database schema");
  char schema[1000]; /* Laaaarge enough! */
  sqlite3_stmt *stmt_users;
  sprintf(schema,
	  "CREATE TABLE IF NOT EXISTS users ("
	  "  username TEXT UNIQUE PRIMARY KEY ON CONFLICT REPLACE,"
	  "  uid      INTEGER CHECK (uid > %d)," // strictly greater
	  "  pwdh     BLOB,"
	  "  last_changed INTEGER,"
	  "  gecos    TEXT,"
	  "  expires  REAL" /* Not using "inserted REAL DEFAULT (strftime('%%s','now'))" */
	  ") WITHOUT ROWID;", options->uid_shift); /* WITHOUT ROWID works only from 3.8.2 */

  sqlite3_prepare_v2(db, schema, -1, &stmt_users, NULL);
  if (!stmt_users || sqlite3_step(stmt_users) != SQLITE_DONE) { D1("ERROR creating users' table: %s", sqlite3_errmsg(db)); }
  sqlite3_finalize(stmt_users);

  sqlite3_stmt *stmt_keys;
  sqlite3_prepare_v2(db,
		     "CREATE TABLE IF NOT EXISTS keys ("
		     "  uid      INTEGER NOT NULL,"
		     "  pubkey   TEXT NOT NULL,"
		     "  PRIMARY KEY (uid, pubkey),"
		     "  FOREIGN KEY (uid) REFERENCES users(uid)"
		     "                    ON DELETE CASCADE ON UPDATE NO ACTION"
		     ");", -1, &stmt_keys, NULL);
  if (!stmt_keys || sqlite3_step(stmt_keys) != SQLITE_DONE) { D1("ERROR creating keys' table: %s", sqlite3_errmsg(db)); }
  sqlite3_finalize(stmt_keys);
  return true;
}

void
cache_close(void)
{
  D2("Closing database cache");
  if(db) sqlite3_close(db);
  cleanconfig();
}


/*
 * Assumes config file already loaded and cache open
 */
int
cache_add_user(const struct fega_user *user)
{
  sqlite3_stmt *stmt = NULL;

  D1("Insert %s into cache", user->username);

  /* The entry will be updated if already present */
  sqlite3_prepare_v2(db, "INSERT INTO users (username,uid,pwdh,last_changed,gecos,expires) VALUES(?1,?2,?3,?4,?5,?6);", -1, &stmt, NULL);
  if(!stmt){ D1("Prepared statement error: %s", sqlite3_errmsg(db)); return false; }

  sqlite3_bind_text(stmt,   1, user->username, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt,    2, user->uid                        );
  sqlite3_bind_blob(stmt,   3, user->pwdh    , -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt,    4, user->last_changed               );
  sqlite3_bind_text(stmt,   5, user->gecos   , -1, SQLITE_STATIC);
  
  unsigned int now = (unsigned int)time(NULL);
  unsigned int expiration = now + options->cache_ttl;
  D2("           Current time to %u", now);
  D2("Setting expiration date to %u", expiration);
  sqlite3_bind_int(stmt, 6, expiration);

  /* We should acquire a RESERVED lock.
     See: https://www.sqlite.org/lockingv3.html#writing
     When the lock is taken, the database returns SQLITE_BUSY.
     So...
     That should be ok with a busy-loop. (Alternative: sleep(0.5)).
     It is highly unlikely that this process will starve.
     All other process will not keep the database busy forever.
  */
  while( sqlite3_step(stmt) == SQLITE_BUSY ); // a RESERVED lock is taken

  /* Execute the query. */
  int rc = (sqlite3_step(stmt) == SQLITE_DONE)?0:1;
  if(rc) D1("Execution error: %s", sqlite3_errmsg(db));
  sqlite3_finalize(stmt);
  stmt = NULL;

  /* Adding the keys */
  if(user->pubkeys){

    struct pbk *pubkeys = user->pubkeys;


    for(; pubkeys; pubkeys = pubkeys->next){
      D2("Insert key %s for user %u", pubkeys->pbk, user->uid);
      sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO keys (uid,pubkey) VALUES(?1,?2);", -1, &stmt, NULL);
      if(!stmt){ D1("Prepared statement error: %s", sqlite3_errmsg(db)); return false; }
      sqlite3_bind_int(stmt,    1, user->uid                        );
      sqlite3_bind_text(stmt,   2, pubkeys->pbk    , -1, SQLITE_STATIC);
      /* We should acquire a RESERVED lock.
	 See: https://www.sqlite.org/lockingv3.html#writing
	 When the lock is taken, the database returns SQLITE_BUSY.
	 So...
	 That should be ok with a busy-loop. (Alternative: sleep(0.5)).
	 It is highly unlikely that this process will starve.
	 All other process will not keep the database busy forever.
      */
      while( sqlite3_step(stmt) == SQLITE_BUSY ); // a RESERVED lock is taken
      /* Execute the query. */
      int rc = (sqlite3_step(stmt) == SQLITE_DONE)?0:1;
      if(rc) D1("Execution error: %s", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      stmt = NULL;
    }
  }

  D1("%s inserted into cache", user->username);

  return rc;
}

static inline int
_col2uid(sqlite3_stmt *stmt, int col, uid_t *uid)
{
  if(sqlite3_column_type(stmt, col) != SQLITE_INTEGER){ D1("Column %d is not a int", col); return 1; }
  *uid = (uid_t)sqlite3_column_int(stmt, col);
  return 0;
}

static inline int
_col2longint(sqlite3_stmt *stmt, int col, long int *i)
{
  if(sqlite3_column_type(stmt, col) != SQLITE_INTEGER){ D1("Column %d is not a int", col); return 1; }
  *i = (long int)sqlite3_column_int64(stmt, col);
  return 0;
}

static inline int
_col2txt(sqlite3_stmt *stmt, int col, char** data, char **buffer, size_t* buflen)
{
  char* s = NULL;
  int type = sqlite3_column_type(stmt, col);
  switch(type){
  case SQLITE_TEXT:
    s = (char*)sqlite3_column_text(stmt, col);
    break;
  case SQLITE_BLOB:
    s = (char*)sqlite3_column_blob(stmt, col);
    break;
  default:
    D1("The colum %d is not a string/blob | got %d", col, type);
    return 1;
    break;
  }
  if( s == NULL ){ D1("Memory allocation error"); return 1; }
  if( copy2buffer(s, data, buffer, buflen) < 0 ) { return -1; }
  return 0;
}

/*
 * 'convert' to struct passwd
 *
 * We use -1 in case the buffer is too small
 *         0 on success
 *         1 on cache miss / user not found
 *         error otherwise
 *
 * Note: Those functions ignore the expiration column
 */

int cache_getpwuid_r(uid_t uid, struct passwd *result, char *buffer, size_t buflen)
{
  sqlite3_stmt *stmt = NULL;
  int rc = 1; /* cache miss */
  D2("select username,uid,gecos from users where uid = %u", uid);
  sqlite3_prepare_v2(db, "select username,uid,gecos from users where uid = ?1 AND expires > strftime('%s', 'now') LIMIT 1",
		     -1, &stmt, NULL);
  if(stmt == NULL){ D1("Prepared statement error: %s", sqlite3_errmsg(db)); return rc; }
  sqlite3_bind_int(stmt, 1, uid);

  /* cache miss */
  if(sqlite3_step(stmt) != SQLITE_ROW) { D2("No SQL row"); goto BAILOUT; }

  /* Convert to struct PWD */
  if( (rc = _col2txt(stmt, 0, &(result->pw_name), &buffer, &buflen)) ) goto BAILOUT;
  if( copy2buffer("x", &(result->pw_passwd), &buffer, &buflen) < 0 ){ rc = -1; goto BAILOUT; }
  result->pw_uid = uid;
  result->pw_gid = options->gid;
  if( (rc = _col2txt(stmt, 2, &(result->pw_gecos), &buffer, &buflen)) ) goto BAILOUT;

  char* homedir = strjoina(options->homedir_prefix, "/", result->pw_name);
  D3("Username %s [%s]", result->pw_name, homedir);
  if( copy2buffer(homedir, &(result->pw_dir), &buffer, &buflen) < 0 ){ rc = -1; goto BAILOUT; }
  if( copy2buffer(options->shell, &(result->pw_shell), &buffer, &buflen) < 0 ){ rc = -1; goto BAILOUT; }

  /* success */ rc = 0;
BAILOUT:
  sqlite3_finalize(stmt);
  return rc;
};

int
cache_getpwnam_r(const char* username, struct passwd *result, char* buffer, size_t buflen)
{
  sqlite3_stmt *stmt = NULL;
  int rc = 1; /* cache miss */
  D2("select uid,gecos from users where username = '%s'", username);
  sqlite3_prepare_v2(db, "select uid,gecos from users where username = ?1 AND expires > strftime('%s', 'now') LIMIT 1",
		     -1, &stmt, NULL);
  if(stmt == NULL){ D1("Prepared statement error: %s", sqlite3_errmsg(db)); return false; }
  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

  /* cache miss */
  if(sqlite3_step(stmt) != SQLITE_ROW) { D2("No SQL row"); goto BAILOUT; }

  /* Convert to struct PWD */
  result->pw_name = (char*)username;
  if( copy2buffer("x"     , &(result->pw_passwd), &buffer, &buflen) < 0 ){ rc = -1; goto BAILOUT; }
  if( (rc = _col2uid(stmt, 0, &(result->pw_uid))) ) goto BAILOUT;
  result->pw_gid = options->gid;
  if( (rc = _col2txt(stmt, 1, &(result->pw_gecos), &buffer, &buflen)) ) goto BAILOUT;

  char* homedir = strjoina(options->homedir_prefix, "/", username);
  D3("Username %s [%s]", username, homedir);
  if( copy2buffer(homedir, &(result->pw_dir), &buffer, &buflen) < 0 ){ rc = -1; goto BAILOUT; }
  if( copy2buffer(options->shell, &(result->pw_shell), &buffer, &buflen) < 0 ){ rc = -1; goto BAILOUT; }

  /* success */ rc = 0;
BAILOUT:
  sqlite3_finalize(stmt);
  return rc;
}

int
cache_getspnam_r(const char* username, struct spwd *result, char* buffer, size_t buflen)
{
  sqlite3_stmt *stmt = NULL;
  int rc = 1; /* cache miss */
  D2("select pwdh, last_changed from users where username = '%s'", username);
  sqlite3_prepare_v2(db, "select pwdh, last_changed from users where username = ?1 AND expires > strftime('%s', 'now') LIMIT 1",
		     -1, &stmt, NULL);
  if(stmt == NULL){ D1("Prepared statement error: %s", sqlite3_errmsg(db)); return false; }
  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

  /* cache miss */
  if(sqlite3_step(stmt) != SQLITE_ROW) { D2("No SQL row"); goto BAILOUT; }

  /* Convert to struct PWD */
  result->sp_namp = (char*)username;
  if( (rc = _col2txt(stmt, 0, &(result->sp_pwdp), &buffer, &buflen)) ) goto BAILOUT;
  if( (rc = _col2longint(stmt, 1, &(result->sp_lstchg))) ) goto BAILOUT;

  result->sp_min = options->sp_min;
  result->sp_max = options->sp_max;
  result->sp_warn = options->sp_warn;
  result->sp_inact = options->sp_inact;
  result->sp_expire = options->sp_expire;

  /* success */ rc = 0;
BAILOUT:
  sqlite3_finalize(stmt);
  return rc;
}

/*
 *
 * The following functions do check the expiration date (in SQL)
 *
 */

bool
cache_print_pubkeys(const char* username)
{
  sqlite3_stmt *stmt = NULL;
  int found = false; /* cache miss */

  D2("select pubkeys for %s", username);
  sqlite3_prepare_v2(db, "select distinct pubkey from users inner join keys on keys.uid = users.uid "
		         "where username = ?1 AND expires > strftime('%s', 'now')", -1, &stmt, NULL);
  if(stmt == NULL){ D1("Prepared statement error: %s", sqlite3_errmsg(db)); return false; }
  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
again:
  if(sqlite3_step(stmt) != SQLITE_ROW) { D2("No SQL row"); goto BAILOUT; } /* cache miss */
  if(sqlite3_column_type(stmt, 0) != SQLITE_TEXT){ D1("The colum 0 is not a string"); goto BAILOUT; }
  const unsigned char* pubkey = sqlite3_column_text(stmt, 0); /* do not free */
  if( !pubkey ){ D1("Memory allocation error"); goto BAILOUT; }
  printf("%s\n", pubkey);
  found = true; /* success */
  goto again;

BAILOUT:
  sqlite3_finalize(stmt);
  return found;
}
