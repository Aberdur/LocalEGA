================
Local EGA Inbox
================

We use the OpenSSH SFTP server (version 9.5), on a Linux
distribution (currently Debian 12).

Authentication is performed by the Operating System, using the classic
plugable mechanism (PAM), and username resolution module (called NSS).

The user's home directory is created when its credentials are
retrieved from CentralEGA. Moreover, we isolate each user in its
respective home directory (i.e. we ``chroot`` the user into it).

We installed hooks inside the OpenSSH SFTP server to detect when a
file is (re)uploaded, renamed or removed, in which case, a
notification is sent to CentralEGA via a `shovel mechanism on the
local message broker`_. In the case of a file upload, the notification
also contains extra file information, such as a SHA256 checksum, its
size and a timestamp for when it was last modified.

We created the SSH deamon ``/opt/openssh/sbin/sshd`` binary and
`configured the *ega* service to use PAM <authentication.html>`_.

The *ega* service is configured using the ``-c`` switch to
specify where the configuration file is. The service runs for the
moment on port 9000.

Note that when PAM is configured as above, and a user is either not
found, or its authentication fails, the access to the service is
denied. No other user (not even root), other than Central EGA users,
have access to that service. We force sftp connections and even
disallow ssh connections on that port.

----

.. toctree::
   :maxdepth: 2
   :name: architecture

   Configuration        <settings>
   Authentication       <authentication>
   Notifications        <notifications>
   Logging/Debugging    <logging>

| Version |version| | Generated |today|

..
   |Codacy| | |Travis| | Version |version| | Generated |today|

   .. |Codacy| image:: https://api.codacy.com/project/badge/Grade/3dd83b28ec2041889bfb13641da76c5b
	   :alt: Codacy Badge
	   :class: inline-baseline

   .. |Travis| image:: https://travis-ci.org/EGA-archive/LocalEGA-inbox.svg?branch=dev
	   :alt: Build Status
	   :class: inline-baseline

.. _shovel mechanism on the local message broker: https://localega.readthedocs.io/en/latest/connection.html
