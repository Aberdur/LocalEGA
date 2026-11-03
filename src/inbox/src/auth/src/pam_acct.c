#include <sys/types.h>
#include <sys/stat.h> /* for chmod */
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <limits.h> /* PATH_MAX and NGROUPS_MAX*/
#include <string.h>
#include <errno.h>

#define PAM_SM_ACCT
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <security/pam_modutil.h>

#include "utils.h"

#define EGA_OPT_SILENT	          (1)
#define EGA_OPT_DEBUG	          (1 << 1)
#define EGA_BAIL_ON_EXISTS        (1 << 2)
#define EGA_ENFORCE_ATTRS         (1 << 3)

struct options_s {
  int flags;
  mode_t attrs;
};

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char *argv[])
{
  D1("Account: Create the homedir");
  const char *username;
  int rc;
  struct options_s opts;

  D3("Getting account PAM module options");
  /* defaults */
  opts.flags = 0;
  opts.attrs = 0755; /* rwxr-xr-x */

  /* Step through module arguments */
  for (; argc-- > 0; ++argv){
    if (!strcmp(*argv, "debug")) {
      opts.flags |= EGA_OPT_DEBUG;
    } else if (!strcmp(*argv, "silent")) {
      opts.flags |= EGA_OPT_SILENT;
    } else if (!strcmp(*argv, "bail_on_exists")) {
      opts.flags |= EGA_BAIL_ON_EXISTS;
    } else if (!strncmp(*argv, "attrs=", 6)) {
      opts.attrs = (mode_t)strtol(*argv+6, NULL, 8); /* octal */
    } else {
      D1("unknown option: %s", *argv);
      /* pam_syslog(pamh, LOG_ERR, "unknown option: %s", *argv); */
      return PAM_PERM_DENIED;
    }
  }

  D3("options: flags = %d", opts.flags);
  D3("options: attrs = %o", opts.attrs);


  /* Determine the user name so we can get the home directory */
  rc = pam_get_user(pamh, &username, NULL);
  if (rc != PAM_SUCCESS || username == NULL || *username == '\0') {
    D1("Cannot obtain the user name: %s", pam_strerror(pamh, rc));
    return PAM_USER_UNKNOWN;
  }

  /* Fetch home directory passwd entry (using NSS) */
  D3("Looking for %s", username);
  struct passwd* user = getpwnam(username);
  //struct passwd* user = pam_modutil_getpwnam(pamh, username);
  if( user == NULL ){ D1("EGA: Unknown user: %s", username); return PAM_ACCT_EXPIRED; }

  D1("Homedir for %s: %s", user->pw_name, user->pw_dir);
  errno = 0;

  /* Create the home directory - not a recursive call. */
  rc = mkdir(user->pw_dir, opts.attrs);
  if(rc && errno != EEXIST){
    D2("unable to create %o %s | rc: %d | %s", opts.attrs, user->pw_dir, rc, strerror(errno));
    return PAM_PERM_DENIED;
  }

  if( rc && errno == EEXIST && (opts.flags & EGA_BAIL_ON_EXISTS)){
    D2("%s exists | Assume success", user->pw_dir);
    return PAM_SUCCESS;
  }
  
  /* enforce ownership */
  rc = chown(user->pw_dir, user->pw_uid, user->pw_gid);
  if(rc){
    D2("unable to change ownership to root | rc: %d | %s", rc, strerror(errno));
    return PAM_PERM_DENIED;
  }
  
  /* enforce default permissions */
  if(opts.flags & EGA_ENFORCE_ATTRS){
    rc = chmod(user->pw_dir, opts.attrs);
    if (rc){
      D2("unable to change permissions to %o | rc: %d | %s", opts.attrs, rc, strerror(errno));
      return PAM_PERM_DENIED;
    }
  }

  return PAM_SUCCESS;
}
