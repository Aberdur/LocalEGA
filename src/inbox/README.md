# LocalEGA-inbox
OpenSSH dropbox, with credentials from CentralEGA and RabbitMQ notifications for file system events


Build the docker image (passing the current user's group, for access permissions), with:

	make latest LEGA_GID=$(id -g)
	# or adjust the group number accordingly

You can run a test instance (not connected to a local broker) with:

	docker-compose up -d

However, you first need to get credentials from Central EGA, and add them to the compose file.  

You then connect to the running instance with:

	sftp -P 2223 username@localhost # adjust the port if needed, including in your compose file

On success, you should get a prompt such as:

	Welcome to Local EGA Demo instance
	Connected to localhost.
	sftp>
