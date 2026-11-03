#include <stdio.h>
#include <sys/types.h>

#include "utils.h"
#include "cache.h"
#include "cega.h"

int
main(int argc, const char **argv)
{
  int rc = 0;

  if( argc < 2 ){ fprintf(stderr, "Usage: %s user\n", argv[0]); return 1; }

  const char* username = argv[1];

  /* check database */
  bool use_cache = options->use_cache && cache_open();
  if(use_cache && cache_print_pubkeys(username)) return rc;

  REPORT("Fetching the public keys from CentralEGA");

  /* Defining the CentralEGA callback */
  int print_pubkey(struct fega_user *user){

    /* assert same name */
    if( strcmp(username, user->username) ){
      REPORT("Requested username %s not matching username response %s", username, user->username);
      return 1;
    }
    if(user->pubkeys){
      struct pbk *current = user->pubkeys;
      while( current ){
	printf("%s\n", current->pbk);
	current = current->next;
      }
    } else {
      REPORT("No ssh key found for user '%s'", username);
    }
    if(use_cache) cache_add_user(user); // ignore result
    return 0;
  }

  char* endpoint = (char*)malloc((options->cega_endpoint_username_len + strlen(username)) * sizeof(char));
  if(!endpoint){ D1("Memory allocation error"); return 1; }

  if(sprintf(endpoint, options->cega_endpoint_username, username) < 0){
    D1("Endpoint formatting error");
    free(endpoint);
    return 2;
  }

  rc = cega_resolve(endpoint, print_pubkey);
  free(endpoint);
  return rc;
}
