An NSS module to find the EGA users in a (remote) database

# Compile the library

	make -C src

# Add it to the system

	make -C src install

	echo '/usr/local/lib/ega' > /etc/ld.so.conf.d/ega.conf
	
	ldconfig -v

`ldconfig` recreates the ld cache and also creates some extra links. (important!).

It is necessary to create `/etc/ega/auth.conf`. Use `auth.conf.sample` as an example.

# Make the system use it

Update `/etc/nsswitch.conf` and add the ega module first, for passwd

	passwd: files ega ...
	shadow: files ega ...

Note: Don't put it first, otherwise it'll search for every users on
the system (eg sshd, root, ...).

Then update your PAM service file. For example, include lines like:

	# module_interface     control_flag     module_name                       module_arguments
	auth                   required         /lib/security/pam_ega_auth.so     use_first_pass
	account                required         /lib/security/pam_ega_acct.so     attrs=0700 bail_on_exists
	#session               required         /lib/security/pam_ega_session.so  umask=0007

See
[the LocalEGA general documentation](http://localega.readthedocs.io)
for further information, and examples.


# How it is build

This repository contains the NSS and PAM modules for Federated EGA.

We use NSS to find out about the users, and PAM to authenticate them
(and chroot them for each session).

When the system needs to know about a specific user, it looks at its
`passwd` database. Above you see that it first looks at its local
files (ie `/etc/passwd`) and then, if the user is not found, it looks
at the _ega_ NSS module.

The EGA NSS module proceed in several steps:

* If the user is found a local cache, and that local cache has not
  expired, it is returned immediately.

* If the user is not found in the cache, we query CentralEGA (with [a
  REST call](https://nss.ega-archive.org/spec/)). If the user doesn't
  exist there, it's the end of the road.

* If the user exists at CentralEGA, we parse the JSON answer and put
  the retrieved user in the local cache.
  
* Upon new requests, only the cache gets queried.

The configuration settings are in `/etc/ega/auth.conf`, and the cache
can be bypassed with `use_cache = no`.

Now that the user is retrieved, the PAM module takes the relay baton.

There are 4 components:

* `auth` is used to challenge the user credentials. We retrieve from
  the cache the user's password hash, which we compare to the one
  supplied by the user.

* `account` is used to check to create the user's home directory
  (which location might vary per Federated EGA site).

* `password` is used to re-create passwords. In our case, we don't
  need it so that component is left unimplemented.

* `session` is used whenever a user passes the authentication step and
  is about the log onto the service (in our case: sshd). When a
  session is open, we refresh the last access date of the user and
  chroot the user into its home directory. We also set the umask.  
  If you use openssh to start a remote session, it already can chroot
  the user in its own directory, so you can skip the PAM session
  setting.

