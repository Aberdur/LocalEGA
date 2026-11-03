#include <curl/curl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>

#include "utils.h"
#include "cache.h"
#include "cega.h"

struct curl_res_s {
  char *body;
  size_t size;
};


/* callback for curl fetch */
size_t
curl_callback (void* contents, size_t size, size_t nmemb, void* userdata) {
  const size_t realsize = size * nmemb;                      /* calculate buffer size */
  struct curl_res_s *r = (struct curl_res_s*) userdata;   /* cast pointer to fetch struct */

  /* expand buffer */
  r->body = (char *) realloc(r->body, r->size + realsize + 1);

  /* check buffer */
  if (r->body == NULL) { D1("ERROR: Failed to expand buffer for cURL"); return -1; }

  /* copy contents to buffer */
  memcpy(&(r->body[r->size]), contents, realsize);
  r->size += realsize;
  r->body[r->size] = '\0';

  return realsize;
}

int
cega_resolve(const char *endpoint, int (*cb)(struct fega_user *user))
{
  int rc = 1; /* error */
  struct curl_res_s* cres = NULL;
  CURL* curl = NULL;
  struct fega_user user;

  D2("Contacting %s", endpoint);
  memset(&user, 0, sizeof(user));
  user.uid = -1;

  /* Preparing cURL */
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();

  if(!curl) { D1("libcurl init failed"); goto BAILOUT; }

  /* Preparing result */
  cres = (struct curl_res_s*)malloc(sizeof(struct curl_res_s));
  if(!cres){ D1("memory allocation failure for the cURL result"); goto BAILOUT; }
  cres->body = NULL;
  cres->size = 0;

  /* Preparing the request */
  curl_easy_setopt(curl, CURLOPT_URL           , endpoint         );
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION , curl_callback    );
  curl_easy_setopt(curl, CURLOPT_WRITEDATA     , (void*)cres      );
  curl_easy_setopt(curl, CURLOPT_FAILONERROR   , 1L               ); /* when not 200 */
  curl_easy_setopt(curl, CURLOPT_HTTPAUTH      , CURLAUTH_BASIC);
  curl_easy_setopt(curl, CURLOPT_USERPWD       , options->cega_creds);
  /* curl_easy_setopt(curl, CURLOPT_NOPROGRESS    , 0L               ); */ /* enable progress meter */

  if ( options->verify_peer && options->cacertfile ){
    D2("Verifying peer settings [CA: %s]", options->cacertfile);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO        , options->cacertfile);
  } else {
    D2("Do not verify peer");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  }

  if ( options->verify_hostname ){
    D2("Check hostname settings");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if ( options->certfile ){
    D2("Adding certfile: %s", options->certfile);
    curl_easy_setopt(curl, CURLOPT_SSLCERT       , options->certfile);
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE   , "PEM"            );

  }

  if ( options->keyfile ){
    D2("Adding keyfile: %s", options->keyfile);
    curl_easy_setopt(curl, CURLOPT_SSLKEY       , options->keyfile);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE   , "PEM"           );
  }

  /* Perform the request */
  CURLcode res = curl_easy_perform(curl);
  if(res != CURLE_OK){ D2("curl_easy_perform() failed: %s", curl_easy_strerror(res)); goto BAILOUT; }

  /* Successful cURL */
  D1("JSON string [size %zu]: %s", cres->size, cres->body);
  
  D2("Parsing the JSON response");
  rc = parse_json(cres->body, cres->size, &user);

  if(rc) { D1("We found %d errors", rc); goto BAILOUT; }

  /* Checking the data */
  if( !user.username ) rc++;
  if( !user.pwdh && !user.pubkeys ) rc++;
  if( user.uid <= 0 ) rc++;
  /* if( !user.gecos ) rc++; */
  if( !user.gecos ) user.gecos = strdup("FEGA User");

  if(rc) { D1("We found %d errors", rc); goto BAILOUT; }

  user.uid += options->uid_shift;

  /* Callback: What to do with the data */
  rc = cb(&user);

BAILOUT:
  if(cres->body)free(cres->body);
  if(cres)free(cres);

  /* cleanup */
  if(user.username) free(user.username);
  if(user.pwdh) free(user.pwdh);
  if(user.gecos) free(user.gecos);

  struct pbk *current = user.pubkeys;
  struct pbk *next = NULL;
  while( current ){
    //D3("Freeing pubkey at %p", current);
    next = current->next;
    if(current->pbk) free(current->pbk);
    free(current);
    current = next;
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return rc;
}
