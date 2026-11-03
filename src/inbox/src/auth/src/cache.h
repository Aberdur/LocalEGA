#ifndef __FEGA_BACKEND_H_INCLUDED__
#define __FEGA_CACHE_H_INCLUDED__

#include <stdbool.h>
#include <pwd.h>
#include <shadow.h>
#include <sqlite3.h>

#if SQLITE_VERSION_NUMBER < 3024000
  #error Only SQLite 3.24+ supported
#endif

#include "config.h"
#include "json.h"

int cache_add_user(const struct fega_user *user);

int cache_getpwnam_r(const char* username, struct passwd *result, char *buffer, size_t buflen);
int cache_getpwuid_r(uid_t uid, struct passwd *result, char *buffer, size_t buflen);
int cache_getspnam_r(const char* username, struct spwd *result, char* buffer, size_t buflen);

bool cache_print_pubkeys(const char* username);

bool cache_open(void);
void cache_close(void);

#endif /* !__FEGA_CACHE_H_INCLUDED__ */
