#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h> /* for umask */

#define PAM_SM_SESSION
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <security/pam_modutil.h>

#include "utils.h"

struct options_s {
  int flags;
  mode_t umask;
};

/*
 * Fetch module options
 */
void pam_options(struct options_s *opts, int argc, const char **argv)
{
  char** args = (char**)argv;
  /* Step through module arguments */
  for (; argc-- > 0; ++args){
    /* if (!strcmp(*args, "silent")) { */
    /*   opts->flags |= PAM_SILENT; */
    /* } else */ if (!strncmp(*argv, "umask=", 6)) {
      opts->umask = (mode_t)strtol(*argv+6, NULL, 8); /* octal */
    } else {
      D1("unknown option: %s", *args);
    }
  }
  return;
}

/*
 * Chroot to homedir
 */
PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
  const char *username;
  int rc;
  struct options_s opts;

  /* defaults */
  opts.flags = 0;
  opts.umask = 0;

  D2("Getting open session PAM module options");
  pam_options(&opts, argc, argv);

  if ( (rc = pam_get_user(pamh, &username, NULL)) != PAM_SUCCESS) { D1("EGA: Unknown user: %s", pam_strerror(pamh, rc)); return rc; }

  /* Get user and make sure the homedir is created */
  struct passwd *user = getpwnam(username);
  if(!user){ D1("Could not find the user '%s'", username); return PAM_SESSION_ERR; }

  /* Handling umask */
  D1("Setting umask to %o", opts.umask);
  umask((mode_t)opts.umask); /* ignore old mask */
  
  D1("Chrooting to %s", user->pw_dir);
  if (chdir(user->pw_dir)) { D1("Unable to chdir to %s: %s", user->pw_dir, strerror(errno)); return PAM_SESSION_ERR; }
  if (chroot(user->pw_dir)){ D1("Unable to chroot(%s): %s", user->pw_dir, strerror(errno)); return PAM_SESSION_ERR; }
  if (chdir("/")){ D1("Unable to chdir(/) after chroot(%s): %s", user->pw_dir, strerror(errno)); return PAM_SESSION_ERR; }

  D1("Session open: Success");
  return PAM_SUCCESS;
}

/*
 * Returning success because we are in the chrooted env
 * so no path to the user's cache entry
 */
PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char *argv[])
{
  D1("Session close: Success");
  return PAM_SUCCESS;
}
