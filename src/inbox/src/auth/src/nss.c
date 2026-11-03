#include <nss.h>
#include <pwd.h>
#include <errno.h>

#include "utils.h"
#include "cache.h"
#include "cega.h"

#define NSS_NAME(func) _nss_ega_ ## func

#define CHECK_CONFIG(ret) do { if(options == NULL) return ret; } while(0)

/* 
 * ===========================================================
 *
 *   Passwd Entry functions
 *
 * =========================================================== 
 */

/* Not allowed */
enum nss_status NSS_NAME(setpwent)(int stayopen){ D1("called"); return NSS_STATUS_UNAVAIL; }
enum nss_status NSS_NAME(endpwent)(void){ D1("called"); return NSS_STATUS_UNAVAIL; }
enum nss_status NSS_NAME(getpwent_r)(struct passwd *result, char *buffer, size_t buflen, int *errnop){ D1("called"); return NSS_STATUS_UNAVAIL; }

enum nss_status
NSS_NAME(getpwuid_r)(uid_t uid, struct passwd *result,
		    char *buffer, size_t buflen, int *errnop)
{
  CHECK_CONFIG(NSS_STATUS_NOTFOUND);
  /* bail out if we're looking for the root user */
  /* if( uid == (uid_t)0 ){ D1("bail out when root"); return NSS_STATUS_NOTFOUND; } */

  if( uid == (uid_t)(-1) ){ D2("ignoring -1"); return NSS_STATUS_NOTFOUND; }

  uid_t ruid = uid - options->uid_shift; 
  D1("Looking up user id %u [remotely %u]", uid, ruid);
  if( ruid <= 0 ){ D2("... too low: ignoring"); return NSS_STATUS_NOTFOUND; }

  int rc = 1;

  bool use_cache = options->use_cache && cache_open();
  if(use_cache){
    
    rc = cache_getpwuid_r(uid, result, buffer, buflen);
    if( rc == -1 ){ D1("Buffer too small"); *errnop = ERANGE; return NSS_STATUS_TRYAGAIN; }
    if( rc == 0  ){ REPORT("User id %u found in cache", uid); *errnop = 0; return NSS_STATUS_SUCCESS; }
    
  }

  D1("Fetching user from CentralEGA");

  /* Defining the callback */
  int cega_callback(struct fega_user *user){

    /* assert same name */
    if( user->uid != uid ){
      REPORT("Requested user id %u not matching user id response %u", uid, user->uid);
      return 1;
    }

    /* Add to database. Ignore result.
     In case the buffer is too small later, it'll fetch the same data from the cache, next time. */
    if(use_cache) cache_add_user(user);

    /* Prepare the answer */
    char* homedir = strjoina(options->homedir_prefix, "/", user->username);
    D1("User id %u [Username %s] [Homedir %s]", user->uid, user->username, homedir);
    if( copy2buffer(user->username, &(result->pw_name)   , &buffer, &buflen) < 0 ) { return -1; }
    if( copy2buffer("x", &(result->pw_passwd), &buffer, &buflen) < 0 ){ return -1; }
    result->pw_uid = user->uid;
    result->pw_gid = options->gid;
    if( copy2buffer(homedir, &(result->pw_dir)   , &buffer, &buflen) < 0 ) { return -1; }
    if( copy2buffer(user->gecos,   &(result->pw_gecos) , &buffer, &buflen) < 0 ) { return -1; }
    if( copy2buffer(options->shell, &(result->pw_shell), &buffer, &buflen) < 0 ) { return -1; }

    return 0;
  }

  char* endpoint = (char*)malloc((options->cega_endpoint_uid_len + 32) * sizeof(char));
  /* Laaaaaaaarge enough! */
  if(!endpoint){ D1("Memory allocation error"); return NSS_STATUS_NOTFOUND; }
  if( sprintf(endpoint, options->cega_endpoint_uid, ruid) < 0 ){
    free(endpoint);
    D1("Error formatting the endpoint");
    return NSS_STATUS_NOTFOUND;
  }
  rc = cega_resolve(endpoint, cega_callback);
  free(endpoint);
  if( rc == -1 ){ D1("Buffer too small"); *errnop = ERANGE; return NSS_STATUS_TRYAGAIN; }
  if( rc > 0 ) { D1("User id %u not found in CentralEGA", uid); return NSS_STATUS_NOTFOUND; }
  *errnop = 0;
  return NSS_STATUS_SUCCESS;
}

/* Find user ny name */
enum nss_status
NSS_NAME(getpwnam_r)(const char *username, struct passwd *result,
		    char *buffer, size_t buflen, int *errnop)
{
  CHECK_CONFIG(NSS_STATUS_NOTFOUND);
  /* bail out if we're looking for the root user */
  /* if( !strcmp(username, "root") ){ D1("bail out when root"); return NSS_STATUS_NOTFOUND; } */

  D1("Looking up '%s'", username);
  /* memset(buffer, '\0', buflen); */

  int rc = 1;

  bool use_cache = options->use_cache && cache_open();
  if(use_cache){
    
    rc = cache_getpwnam_r(username, result, buffer, buflen);
    if( rc == -1 ){ D1("Buffer too small"); *errnop = ERANGE; return NSS_STATUS_TRYAGAIN; }
    if( rc == 0  ){ REPORT("User %s found in cache", username); *errnop = 0; return NSS_STATUS_SUCCESS; }
    
  }

  D1("Fetching user from CentralEGA");

  /* Defining the callback */
  int cega_callback(struct fega_user *user){

    /* assert same name */
    if( strcmp(username, user->username) ){
      REPORT("Requested username %s not matching username response %s", username, user->username);
      return 1;
    }

    /* Add to database. Ignore result.
     In case the buffer is too small later, it'll fetch the same data from the cache, next time. */
    if(use_cache) cache_add_user(user);

    /* Prepare the answer */
    char* homedir = strjoina(options->homedir_prefix, "/", username);
    D1("Username %s [Homedir %s]", user->username, homedir);
    result->pw_name = (char*)username; /* no need to copy to buffer */
    if( copy2buffer("x", &(result->pw_passwd), &buffer, &buflen) < 0 ){ return -1; }
    result->pw_uid = user->uid;
    result->pw_gid = options->gid;
    if( copy2buffer(homedir, &(result->pw_dir)   , &buffer, &buflen) < 0 ) { return -1; }
    if( copy2buffer(user->gecos,   &(result->pw_gecos) , &buffer, &buflen) < 0 ) { return -1; }
    if( copy2buffer(options->shell, &(result->pw_shell), &buffer, &buflen) < 0 ) { return -1; }
  
    return 0;
  }

  char* endpoint = (char*)malloc((options->cega_endpoint_username_len + strlen(username)) * sizeof(char));
  if(!endpoint){ D1("Memory allocation error"); return NSS_STATUS_NOTFOUND; }
  if( sprintf(endpoint, options->cega_endpoint_username, username) < 0 ){
    free(endpoint);
    D1("Error formatting the endpoint");
    return NSS_STATUS_NOTFOUND;
  }
  rc = cega_resolve(endpoint, cega_callback);
  free(endpoint);
  if( rc == -1 ){ D1("Buffer too small"); *errnop = ERANGE; return NSS_STATUS_TRYAGAIN; }
  if( rc > 0 ) { D1("User %s not found in CentralEGA", username); return NSS_STATUS_NOTFOUND; }
  REPORT("User %s found in CentralEGA", username);
  *errnop = 0;
  return NSS_STATUS_SUCCESS;
}

/* 
 * ===========================================================
 *
 *   Shadow Entry functions
 *
 * =========================================================== 
 */

/* Not allowed */
enum nss_status NSS_NAME(setspent)(int stayopen){ D1("called"); return NSS_STATUS_UNAVAIL; }
enum nss_status NSS_NAME(endspent)(void){ D1("called"); return NSS_STATUS_UNAVAIL; }
enum nss_status NSS_NAME(getspent_r)(struct spwd *result, char *buffer, size_t buflen, int *errnop){ D1("called"); return NSS_STATUS_UNAVAIL; }

enum nss_status
NSS_NAME(getspnam_r)(const char *username, struct spwd *result,
		     char *buffer, size_t buflen, int *errnop)
{
  CHECK_CONFIG(NSS_STATUS_NOTFOUND);

  /* Only the config file group owner can do that */
  if( getgid() != options->shadow_gid ){ D2("you are allowed"); return NSS_STATUS_UNAVAIL; }

  /* bail out if we're looking for the root user */
  /* if( !strcmp(username, "root") ){ D1("bail out when root"); return NSS_STATUS_NOTFOUND; } */

  D1("Looking up '%s'", username);
  /* memset(buffer, '\0', buflen); */

  int rc = 1;

  bool use_cache = options->use_cache && cache_open();
  if(use_cache){
    
    rc = cache_getspnam_r(username, result, buffer, buflen);
    if( rc == -1 ){ D1("Buffer too small"); *errnop = ERANGE; return NSS_STATUS_TRYAGAIN; }
    if( rc == 0  ){ REPORT("User %s found in cache", username); *errnop = 0; return NSS_STATUS_SUCCESS; }
    
  }


  D1("Fetching user from CentralEGA");

  /* Defining the callback */
  int cega_callback(struct fega_user *user){

    /* assert same name */
    if( strcmp(username, user->username) ){
      REPORT("Requested username %s not matching username response %s", username, user->username);
      return 1;
    }

    /* Add to database. Ignore result.
     In case the buffer is too small later, it'll fetch the same data from the cache, next time. */
    if(use_cache) cache_add_user(user);

    /* Prepare the answer */
    result->sp_namp = (char*)username; /* no need to copy to buffer */
    if( copy2buffer(user->pwdh, &(result->sp_pwdp), &buffer, &buflen) < 0 ){ return -1; }
    result->sp_lstchg = user->last_changed;
    result->sp_min = options->sp_min;
    result->sp_max = options->sp_max;
    result->sp_warn = options->sp_warn;
    result->sp_inact = options->sp_inact;
    result->sp_expire = options->sp_expire;
  
    return 0;
  }

  char* endpoint = (char*)malloc((options->cega_endpoint_username_len + strlen(username)) * sizeof(char));
  if(!endpoint){ D1("Memory allocation error"); return NSS_STATUS_NOTFOUND; }
  if( sprintf(endpoint, options->cega_endpoint_username, username) < 0 ){
    free(endpoint);
    D1("Error formatting the endpoint");
    return NSS_STATUS_NOTFOUND;
  }
  rc = cega_resolve(endpoint, cega_callback);
  free(endpoint);
  if( rc == -1 ){ D1("Buffer too small"); *errnop = ERANGE; return NSS_STATUS_TRYAGAIN; }
  if( rc > 0 ) { D1("User %s not found in CentralEGA", username); return NSS_STATUS_NOTFOUND; }
  REPORT("User %s found in CentralEGA", username);
  *errnop = 0;
  return NSS_STATUS_SUCCESS;
}

/*
 * Finally: No group functions here
 */

