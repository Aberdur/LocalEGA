FROM debian:12-slim AS BUILD

RUN apt-get update && \
#    apt-get upgrade && \
    apt-get install -y --no-install-recommends \
            vim ca-certificates pkg-config git gcc cmake make automake autoconf libtool patch \
            bzip2 zlib1g-dev libssl-dev libedit-dev libcurl4-openssl-dev procps \
            libjson-c-dev libsqlite3-dev libpam0g-dev uuid-dev libreadline-dev librabbitmq-dev

RUN groupadd -g 75 -r ega-sshd && \
    mkdir -p /var/empty/sshd && \
    chmod 700 /var/empty/sshd && \
# /var/empty/sshd must be owned by root and not group or world-writable.
    useradd -c "Privilege-separated SSH" \
            -u 75 \
            -g ega-sshd \
            -s /usr/sbin/nologin \
            -r \
            -d /var/empty/sshd ega-sshd

COPY src /var/src
WORKDIR /var/src/openssh

# Patching the sftp-server.c/sshd.c
RUN patch -p1 < ../patches/lega.patch

# (re)Build OpenSSH
RUN autoreconf && \
    ./configure --prefix=/opt/openssh \
                --with-pam --with-pam-service=ega \
		--with-zlib \
		--with-openssl \
		--with-libedit \
		--with-privsep-user=ega-sshd \
	        --with-privsep-path=/var/empty/sshd \
	        --without-xauth \
 	        --without-maildir \
		--without-selinux \
		--without-systemd \
		--with-pid-dir=/run && \
     make && \
# rsa, dsa and ed25519 keys are created in the entrypoint
     make install-nosysconf

# Install EGA PAM
WORKDIR /var/src/auth/src
RUN mkdir -p /usr/local/lib/ega && \
    make install clean

#################################################
## DEV running
#################################################

COPY conf/sshd_config /etc/ega/sshd_config
COPY conf/pam.ega /etc/pam.d/ega

ARG LEGA_GID=1000
RUN groupadd -r -g ${LEGA_GID} lega # will fail on purpose if the user passed an existing group inside the container
RUN echo '/usr/local/lib' >> /etc/ld.so.conf.d/ega.conf && \
    echo '/usr/local/lib/ega' >> /etc/ld.so.conf.d/ega.conf && \
    sed -i -e 's/^passwd:\(.*\)files/passwd:\1files ega/' /etc/nsswitch.conf && \
    sed -i -e 's/^shadow:\(.*\)files/shadow:\1files ega/' /etc/nsswitch.conf && \
    ldconfig -v

COPY conf/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod 755 /usr/local/bin/entrypoint.sh

ENTRYPOINT ["entrypoint.sh"]

#################################################
##
## Final image
##
#################################################

FROM debian:12-slim

LABEL maintainer "EGA System Developers"
LABEL org.label-schema.schema-version="1.0"
LABEL org.label-schema.vcs-url="https://github.com/EGA-archive/LocalEGA-inbox"

EXPOSE 9000
VOLUME /ega/inbox

# Before the EGA PAM lib is loaded
ARG LEGA_GID=1000

RUN groupadd -g 75 -r ega-sshd && \
    mkdir -p /var/empty/sshd && \
    chmod 700 /var/empty/sshd && \
# /var/empty/sshd must be owned by root and not group or world-writable.
    useradd -c "Privilege-separated SSH" \
            -u 75 \
            -g ega-sshd \
            -s /usr/sbin/nologin \
            -r \
            -d /var/empty/sshd ega-sshd && \
    groupadd -r -g ${LEGA_GID} lega # will fail on purpose if the user passed an existing group inside the container

ARG ARCH=x86_64    

COPY --from=BUILD /opt/openssh /opt/openssh
COPY --from=BUILD /usr/local/bin /usr/local/bin
COPY --from=BUILD /usr/local/lib /usr/local/lib
COPY --from=BUILD /usr/lib/$ARCH-linux-gnu/ /usr/lib/$ARCH-linux-gnu/

#COPY --from=BUILD /lib/security/pam_ega_* /lib/security/
COPY --from=BUILD /lib/security/pam_ega_auth.so /lib/security/pam_ega_auth.so
COPY --from=BUILD /lib/security/pam_ega_acct.so /lib/security/pam_ega_acct.so
COPY --from=BUILD /lib/security/pam_ega_session.so /lib/security/pam_ega_session.so


COPY conf/sshd_config /etc/ega/sshd_config
COPY conf/pam.ega /etc/pam.d/ega
COPY conf/entrypoint.sh /usr/local/bin/entrypoint.sh

RUN chmod 755 /usr/local/bin/entrypoint.sh && \
    echo '/usr/local/lib' >> /etc/ld.so.conf.d/ega.conf && \
    echo '/usr/local/lib/ega' >> /etc/ld.so.conf.d/ega.conf && \
    sed -i -e 's/^passwd:\(.*\)files/passwd:\1files ega/' /etc/nsswitch.conf && \
    sed -i -e 's/^shadow:\(.*\)files/shadow:\1files ega/' /etc/nsswitch.conf && \
    ldconfig -v

ENTRYPOINT ["entrypoint.sh"]

ARG COMMIT
ARG BUILD_DATE
LABEL org.label-schema.build-date=$BUILD_DATE
LABEL org.label-schema.vcs-ref=$COMMIT
