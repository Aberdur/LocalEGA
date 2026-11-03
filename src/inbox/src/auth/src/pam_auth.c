#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <crypt.h>
#include <time.h>
#include <shadow.h>
#include <pwd.h>

#define PAM_SM_AUTH
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <security/pam_modutil.h>

/* #define _OW_SOURCE */
//#include "blowfish/ow-crypt.h"
#include "blowfish/crypt_blowfish.h"

#include "utils.h"

#define EGA_DEFAULT_PROMPT "Please enter your EGA password: "

#define PAM_OPT_DEBUG			0x01
#define PAM_OPT_USE_FIRST_PASS		0x02
#define	PAM_OPT_TRY_FIRST_PASS		0x04
#define	PAM_OPT_ECHO_PASS		0x08

struct options_s {
  int flags;
  char* prompt;
};

/*
 * Fetch module options
 */
void pam_options(struct options_s *opts, int argc, const char **argv)
{
  char** args = (char**)argv;
  /* Step through module arguments */
  for (; argc-- > 0; ++args){
    if (!strcmp(*args, "silent")) {
      opts->flags |= PAM_SILENT;
    } else if (!strcmp(*args, "debug")) {
      opts->flags |= PAM_OPT_DEBUG;
    } else if (!strcmp(*args, "use_first_pass")) {
      opts->flags |= PAM_OPT_USE_FIRST_PASS;
    } else if (!strcmp(*args, "try_first_pass")) {
      opts->flags |= PAM_OPT_TRY_FIRST_PASS;
    } else if (!strcmp(*args, "echo_pass")) {
      opts->flags |= PAM_OPT_ECHO_PASS;
    } else if (!strncmp(*args,"prompt=",7)) {
      opts->prompt = *args+7;
    } else {
      D1("unknown option: %s", *args);
    }
  }
  return;
}

static int timingsafe_bcmp(const void *b1, const void *b2, size_t n);
#define MIN(a,b) ((a)<(b))?(a):(b)
/*
 * authenticate user
 */
PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
  const char *user = NULL, *password = NULL;
  const void *item;
  int rc;
  const struct pam_conv *conv;
  struct pam_message msg;
  const struct pam_message *msgs[1];
  struct pam_response *resp;
  struct options_s opts;

  opts.flags = 0;
  opts.prompt = EGA_DEFAULT_PROMPT;
  
  D2("Getting auth PAM module options");

  rc = pam_get_user(pamh, &user, NULL);
  if (rc != PAM_SUCCESS) { D1("Can't get user: %s", pam_strerror(pamh, rc)); return rc; }
  
  rc = pam_get_item(pamh, PAM_RHOST, &item);
  if ( rc != PAM_SUCCESS) { D1("EGA: Unknown rhost: %s", pam_strerror(pamh, rc)); }
  D1("Authenticating %s%s%s", user, (item)?" from ":"", (item)?((char*)item):"");

  pam_options(&opts, argc, argv);

  /* Grab the already-entered password if we might want to use it. */
  if (opts.flags & (PAM_OPT_TRY_FIRST_PASS | PAM_OPT_USE_FIRST_PASS)){
    rc = pam_get_item(pamh, PAM_AUTHTOK, &item);
    if (rc != PAM_SUCCESS){
      D1("(already-entered) password retrieval failed: %s", pam_strerror(pamh, rc));
      return rc;
    }
  }

  password = (char*)item;
  /* The user hasn't entered a password yet. */
  if (!password && (opts.flags & PAM_OPT_USE_FIRST_PASS)){
    D1("Password retrieval failed: %s", pam_strerror(pamh, rc));
    return PAM_AUTH_ERR;
  }

  D1("Asking %s for password", user);

  /* Get the password then */
  msg.msg_style = (opts.flags & PAM_OPT_ECHO_PASS)?PAM_PROMPT_ECHO_ON:PAM_PROMPT_ECHO_OFF;
  msg.msg = opts.prompt;
  msgs[0] = &msg;

  rc = pam_get_item(pamh, PAM_CONV, &item);
  if (rc != PAM_SUCCESS){ D1("Conversation initialization failed: %s", pam_strerror(pamh, rc)); return rc; }

  conv = (struct pam_conv *)item;
  rc = conv->conv(1, msgs, &resp, conv->appdata_ptr);
  if (rc != PAM_SUCCESS){ D1("Password conversation failed: %s", pam_strerror(pamh, rc)); return rc; }
  
  rc = pam_set_item(pamh, PAM_AUTHTOK, (const void*)resp[0].resp);
  if (rc != PAM_SUCCESS){ D1("Setting password for other modules failed: %s", pam_strerror(pamh, rc)); return rc; }

  /* Cleaning the message */
  memset(resp[0].resp, 0, strlen(resp[0].resp));
  free(resp[0].resp);
  free(resp);

  D1("Get it again after conversation");

  rc = pam_get_item(pamh, PAM_AUTHTOK, &item);
  password = (char*)item;
  if (rc != PAM_SUCCESS){ D1("Password retrieval failed: %s", pam_strerror(pamh, rc)); return rc; }

  D1("Allowing empty passwords?");
  /* Check if empty password are disallowed */
  if ((!password || !*password) && (flags & PAM_DISALLOW_NULL_AUTHTOK)) { return PAM_AUTH_ERR; }
  
  /* Now, we have the password */
  D1("Authenticating user %s with password", user);

  struct spwd *shadow = getspnam(user);
  if(!shadow){ D1("Could not load the password hash of '%s'", user); return PAM_AUTH_ERR; }

  size_t phlen = (shadow->sp_pwdp == NULL)?0:strlen(shadow->sp_pwdp);

  if(!strncmp(shadow->sp_pwdp, "$2", 2)){
    D2("Using Blowfish");
    char pwdh_computed[64];
    memset(pwdh_computed, '\0', 64);

    if(_crypt_blowfish_rn(password, shadow->sp_pwdp, pwdh_computed, 64) == NULL){
      D2("bcrypt failed: %s", strerror(errno));
      return PAM_AUTH_ERR;
    }

    //if(!strcmp(password_hash, (char*)pwdh_computed)) { return PAM_SUCCESS; }
    if(phlen == strlen(pwdh_computed) &&
       !timingsafe_bcmp(shadow->sp_pwdp, (char*)pwdh_computed, phlen)) { return PAM_SUCCESS; }

  } else {
    D2("Using libc: supporting MD5, SHA256, SHA512");
    char *pwdh_computed = crypt(password, shadow->sp_pwdp);
    if(phlen == strlen(pwdh_computed) &&
       !timingsafe_bcmp(shadow->sp_pwdp, pwdh_computed, phlen)) { return PAM_SUCCESS; }
  }

  D1("Authentication failed for %s", user);
  return PAM_AUTH_ERR;
}

/*
 * setcred runs before and after session_open
 * which means, 'after' is in a chrooted-env, so setcred fails
 * (but before succeeds)
 * So a user is refreshed right before an attempts to open a session,
 * right after a successful authentication
 */
PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
  /* D1("Set cred ignored"); */
  /* return PAM_IGNORE; */
  D1("Set cred allowed");
  return PAM_SUCCESS;
}

static int
timingsafe_bcmp(const void *b1, const void *b2, size_t n)
{
  const unsigned char *p1 = b1, *p2 = b2;
  int ret = 0;

  for (; n > 0; n--)
    ret |= *p1++ ^ *p2++;
  return (ret != 0);
}
