
#ifndef __FEGA_CONFIG_H_INCLUDED__
#define __FEGA_CONFIG_H_INCLUDED__

#include <stdbool.h>
#include <sys/types.h> 

struct options_s {
  char* cfgfile;
  char* buffer;
  
  gid_t gid;               /* group id for all EGA users */
  uid_t uid_shift;         /* added to the user id from CentralEGA */
  char* prompt;            /* Please enter password */
  char* shell;             /* Please enter password */
  char* homedir_prefix;    /* EGA main inbox directory */

  gid_t shadow_gid;  /* group id of the config file owner */

  /* For shadow entries */
  long int sp_min; /* days until change allowed. */
  long int sp_max; /* days before change required */
  long int sp_warn; /* days warning for expiration */
  long int sp_inact; /* days before account inactive */
  long int sp_expire; /* date when account expires */

  /* Cache */
  bool use_cache;           /* use it / bypass it */
  char* db_path;           /* db file path */
  unsigned int cache_ttl;  /* How long a cache entry is valid (in seconds) */


  /* Contacting Central EGA (via a REST call) */
  char* cega_endpoint_username; /* string format with one %s, replaced by username | returns a triplet in JSON format */
  size_t cega_endpoint_username_len; /* its length, -2 (for %s) */

  char* cega_endpoint_uid;      /* string format with one %s, replaced by uid      | idem */
  size_t cega_endpoint_uid_len; /* its length, -2 (for %s) */

  char* cega_creds;        /* for authentication: user:password */

  char* cacertfile;        /* path to the Root certificate to contact Central EGA */
  char* certfile;          /* For client verification */
  char* keyfile;
  bool verify_peer;
  bool verify_hostname;
};

typedef struct options_s options_t;

extern options_t* options;

bool loadconfig(void);
void cleanconfig(void);

#endif /* !__FEGA_CONFIG_H_INCLUDED__ */
