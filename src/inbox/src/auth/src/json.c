#include <string.h>

#include "utils.h"
#include "config.h"
#include "json.h"


/**
 *  Accepted JSON format: 
 *  --------------------
 *
 *     {
 *       "sshPublicKeys" : array of strings,
 *       "username" : string,
 *       "passwordHash" : string,
 *       "uid" : int,
 *       "gecos" : string,
 *       "lastChanged" : long int
 *     }
 *
 */

#define CEGA_JSON_USER  "username"
#define CEGA_JSON_UID   "uid"
#define CEGA_JSON_PWD   "passwordHash"
#define CEGA_JSON_PBK   "sshPublicKeys"
#define CEGA_JSON_GECOS "gecos"
#define CEGA_JSON_LSTCHG "lastChanged"

#ifdef DEBUG
#define TYPE2STR(t) (((t) == JSMN_OBJECT)   ? "Object":    \
                     ((t) == JSMN_ARRAY)    ? "Array":     \
                     ((t) == JSMN_STRING)   ? "String":    \
                     ((t) == JSMN_PRIMITIVE)? "Primitive": \
                                              "Undefined")
#endif


#if 0
static int
get_size(jsmntok_t *t){
  int i, j;
  if (t->type == JSMN_PRIMITIVE || t->type == JSMN_STRING) {
    if(t->size > 0) return get_size(t+1)+1;
    return 1;
  } else if (t->type == JSMN_OBJECT || t->type == JSMN_ARRAY) {
    j = 0;
    for (i = 0; i < t->size; i++) { j += get_size(t+1+j); }
    return j+1;
  } else {
    D1("get_size: weird type %s", TYPE2STR(t->type));
    return 1000000;
  }
}
#endif

#define KEYEQ(json, t, s) ((int)strlen(s) == ((t)->end - (t)->start)) && strncmp((json) + (t)->start, s, (t)->end - (t)->start) == 0

int
parse_json(const char* json, int jsonlen, struct fega_user *user)
{
  jsmn_parser jsonparser; /* on the stack */
  jsmntok_t *tokens = NULL; /* array of tokens */
  size_t size_guess = 15; /* 6*2 (key:value) + 1(object) + 2 sshkeys */
  int r, rc=1;

REALLOC:
  /* Initialize parser (for every guess) */
  jsmn_init(&jsonparser);
  D2("Guessing with %zu tokens", size_guess);
  if(tokens)free(tokens);
  tokens = malloc(sizeof(jsmntok_t) * size_guess);
  if (tokens == NULL) { D1("memory allocation error"); goto BAILOUT; }
  r = jsmn_parse(&jsonparser, json, jsonlen, tokens, size_guess);
  if (r < 0) { /* error */
    D2("JSON parsing error: %s", (r == JSMN_ERROR_INVAL)? "JSON string is corrupted" :
                                 (r == JSMN_ERROR_PART) ? "Incomplete JSON string":
                                 (r == JSMN_ERROR_NOMEM)? "Not enough space in token array":
                                                          "Unknown error");
    if (r == JSMN_ERROR_NOMEM) {
      size_guess = size_guess * 2; /* double it */
      goto REALLOC;
    }
    goto BAILOUT;
  }

  /* Valid response */
  D3("%d tokens found", r);
  if( tokens->type != JSMN_OBJECT ){ D1("JSON object expected"); rc = 1; goto BAILOUT; }
  if( r<7 ){ D1("We should get at least 7 tokens"); rc = 1; goto BAILOUT; }

  D1("ROOT %.*s [%d items]", tokens->end-tokens->start, json + tokens->start, tokens->size);

  jsmntok_t *t = tokens; /* sentinel */
  int max = t->size;
  int i,j;
  t++; /* move inside the root */
  rc = 0; /* assume success */
  for (i = 0; i < max; i++, t+=t->size+1) {

    if(t->type == JSMN_STRING){

      if( KEYEQ(json, t, CEGA_JSON_USER) ){
	t+=t->size; /* get to the value */
	if(user->username){ D3("Strange! I already have username"); continue; }
	if(t->type == JSMN_STRING) /* not null */
	  user->username = strndup(json + t->start, t->end-t->start);
      } else if( KEYEQ(json, t, CEGA_JSON_PWD) ){
	t+=t->size; /* get to the value */
	if(user->pwdh){ D3("Strange! I already have pwdh"); continue; }
	if(t->type == JSMN_STRING) /* not null */
	  user->pwdh = strndup(json + t->start, t->end-t->start);
      } else if( KEYEQ(json, t, CEGA_JSON_GECOS) ){
	t+=t->size; /* get to the value */
	if(user->gecos){ D3("Strange! I already have gecos"); continue; }
	if(t->type == JSMN_STRING) /* not null */
	  user->gecos = strndup(json + t->start, t->end-t->start);
      } else if( KEYEQ(json, t, CEGA_JSON_PBK) ){
	t+=t->size; /* get to the value */
	/* parse array */
	jsmntok_t *k = t; /* sentinel */
	int nkeys = k->size;
	//D1("KEYS %s %.*s [%d items]", TYPE2STR(k->type), k->end-k->start, json + k->start, k->size);
	if(k->type == JSMN_ARRAY && nkeys > 0){
	  if(user->pubkeys){ D3("Strange! I already have pubkeys"); continue; }
	  k++; /* go inside */
	  struct pbk *current = (struct pbk *)malloc(sizeof(struct pbk));
	  if(!current){ D1("memory allocation error"); goto BAILOUT; }
	  struct pbk *prev = NULL;
	  user->pubkeys = current;
	  for (j = 0; j < nkeys; j++, k+=k->size+1) {
	    current->pbk = strndup(json + k->start, k->end-k->start);
	    current->next = NULL;
	    if(prev) prev->next = current;
	    prev = current;
	    current = (struct pbk *)malloc(sizeof(struct pbk));
	    if(!current){ D1("memory allocation error"); goto BAILOUT; }
	  }
	}
      } else if( KEYEQ(json, t, CEGA_JSON_UID) ){
	t+=t->size; /* get to the value */
	if(t->type == JSMN_PRIMITIVE){ /* number */
	  char* cend;
	  int uid = strtol(json + t->start, (char**)&cend, 10); /* reuse cend above */
	  if( (cend == (json + t->end)) ) /* else: error when cend does not point to end+1 */
	    user->uid = uid; 
	}
      } else if( KEYEQ(json, t, CEGA_JSON_LSTCHG) ){
	t+=t->size; /* get to the value */
	if(t->type == JSMN_PRIMITIVE){ /* number */
	  char* cend;
	  long int last_changed = strtol(json + t->start, (char**)&cend, 10); /* reuse cend above */
	  if( (cend == (json + t->end)) ) /* else: error when cend does not point to end+1 */
	    user->last_changed = last_changed; 
	}
      } else {
	D3("Unexpected key: %.*s with %d items", t->end-t->start, json + t->start, t->size);
	t+=t->size; /* get to the value */
	D3("of type %s with %d items", TYPE2STR(t->type), t->size);
      }
    } else {
      D2("Not a string token");
      rc++;
    }
  }

#ifdef DEBUG
  if(rc) D1("%d errors while parsing the root object", rc);
#endif

BAILOUT:
  if(tokens){ D3("Freeing tokens at %p", tokens); free(tokens); }
  return rc;
}
