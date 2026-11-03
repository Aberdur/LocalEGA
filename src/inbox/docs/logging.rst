Logging & Debugging
===================

We leverage the logging capabilites of OpenSSH. It can use ``syslog``,
and since we (might) run the ``sftp-server`` in a chroot environment,
the syslog socket ``/dev/log`` is not found.

Here are a few steps to create it. Let's say we want to get the logs
for the user *john*.  Create the directory ``/ega/inbox/john/dev``
(since ``/ega/inbox/john`` is the home directory for ``john``).
Install ``rsyslod``, and configure it by creating the file
``/etc/rsyslog.d/sftp.conf`` with

.. code-block:: none

		# create additional sockets for the sftp chrooted users
		module(load="imuxsock")
		input(type="imuxsock" Socket="/ega/inbox/john/dev/log" CreatePath="on")

		# log internal-sftp activity to sftp.log
		if $programname == 'internal-sftp' then /var/log/sftp.log
		& stop

This will create the ``/dev/log`` socket in the environment where the
ssh daemon in running (ie, in john's home directory).

Finally, you can update the LogLevel setting in the
``/etc/ega/sshd_config`` configuration file, or pass it as
command-line argument with ``-o LogLevel=<level>``. ``INFO`` and
``VERBOSE`` are useful examples of `LogLevel`_.

You should now see the logs in ``/var/log/sftp.log``.

.. _LogLevel: https://linux.die.net/man/5/sshd_config

.. note:: While compiling and installing use ``make && make install``,
   you can see more output information by compiling the ega-sshd with
   more output, using ``make debug1``, ``make debug2`` or ``make
   debug3``.
