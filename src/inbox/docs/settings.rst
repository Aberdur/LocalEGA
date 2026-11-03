Configuration settings
======================

OpenSSH SFTP server
-------------------

We configure the OpenSSH deamon to run on port 9000, and only allow
sftp connections, for users in the ``lega`` group. (We don't even
allow root, nor ssh).

We disable X11 forwarding, tunneling and other forms of relay. We
allow password and public key authentication.

The configuration file is in ``/etc/ega/sshd_config`` as:

.. code::

   #LogLevel VERBOSE
   Port 9000
   Protocol 2
   Banner  /etc/ega/banner
   HostKey /etc/ega/ssh_host_rsa_key
   HostKey /etc/ega/ssh_host_dsa_key
   HostKey /etc/ega/ssh_host_ed25519_key
   # Authentication
   UsePAM yes
   AuthenticationMethods "publickey" "keyboard-interactive:pam"
   PubkeyAuthentication yes
   PasswordAuthentication no
   ChallengeResponseAuthentication yes
   # Faster connection
   UseDNS no
   # Limited access
   DenyGroups *,!lega
   DenyUsers root lega
   PermitRootLogin no
   X11Forwarding no
   AllowTcpForwarding no
   PermitTunnel no
   AcceptEnv LANG LC_CTYPE LC_NUMERIC LC_TIME LC_COLLATE LC_MONETARY LC_MESSAGES
   AcceptEnv LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE LC_MEASUREMENT
   AcceptEnv LC_IDENTIFICATION LC_ALL LANGUAGE
   AcceptEnv XMODIFIERS
   Subsystem sftp internal-sftp #-l INFO
   AuthorizedKeysCommand /usr/local/bin/ega_ssh_keys
   AuthorizedKeysCommandUser root
   


Message Broker
--------------

The message broker connection is configured in the file ``/etc/ega/mq.conf`` as follows:


.. code::

   ##############################
   # Message Broker configuration
   ##############################

   # of the form amqp(s)://user:password@host:port/vhost
   connection = ${MQ_CONNECTION}

   # Where to send the notifications
   exchange = ${MQ_EXCHANGE:-cega}
   routing_key = ${MQ_ROUTING_KEY:-files.inbox}

   # Sets the message broker's heartbeat (in seconds).
   # Default: 0 (ie disabled).
   heartbeat = 0

   # This causes the TLS layer to 
   # verify the hostname in the certificate.
   # (Only valid when using "amqps")
   # Default: no
   verify_hostname = no

   # This causes the TLS layer to 
   # verify the server's certificate.
   # (Only valid when using "amqps")
   # Default: no (as RabbitMQ's certificate is self-signed).
   verify_peer = no

   # If case verify_peer = yes, this causes the 
   # sftp server to use the given trusted bundle.
   #cacert = /path/to/ca/trusted/bundle


When the docker image is booted, the following environment variables
are used to create the above configuration file.

+------------------+---------------+-------------------------------------------------+
| Variable name    | Default value | Example/Format                                  |
+==================+===============+=================================================+
| MQ_CONNECTION *  |               | amqp(s)://username:password@hostname:port/vhost |
+------------------+---------------+-------------------------------------------------+
| MQ_EXCHANGE      | cega          | The exchange name                               |
+------------------+---------------+-------------------------------------------------+
| MQ_ROUTING_KEY   | files.inbox   | The routing key for the messages                |
+------------------+---------------+-------------------------------------------------+

`*` required


.. note:: RSA (/etc/ega/ssh_host_rsa_key), DSA
          (/etc/ega/ssh_host_dsa_key) and ed25519
          (/etc/ega/ssh_host_ed25519_key) keys are (re)created at boot
          time.
