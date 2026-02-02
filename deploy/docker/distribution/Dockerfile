FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config git gcc g++ make cmake automake autoconf libtool \
    libfuse3-dev libglib2.0-dev libpq-dev libssl-dev libpam0g-dev libedit-dev \
    zlib1g-dev libsystemd-dev patch ca-certificates util-linux libcap2-bin fuse3 \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    groupadd -r lega; \
    useradd -r -g lega -s /usr/sbin/nologin lega; \
    groupadd -r requesters; \
    groupadd -r -g 999 postgres || groupadd -r postgres; \
    useradd -r -u 999 -g postgres -s /usr/sbin/nologin postgres || \
      useradd -r -g postgres -s /usr/sbin/nologin postgres; \
    useradd -r -s /usr/sbin/nologin ega-sshd

RUN mkdir -p /opt/LocalEGA/etc/sshd /opt/LocalEGA/etc/nss /opt/LocalEGA/homes /opt/LocalEGA/security \
    && echo "user_allow_other" >> /etc/fuse.conf

COPY src/distribution/src /tmp/distribution/src

WORKDIR /tmp/distribution/src/crypt4gh-fs
RUN autoreconf -i \
    && ./configure --prefix=/opt/LocalEGA \
    && make \
    && make install

WORKDIR /tmp/distribution/src/nss
RUN make \
    && make install EGA_LIBDIR=/opt/LocalEGA/lib \
    && echo '/opt/LocalEGA/lib' > /etc/ld.so.conf.d/LocalEGA.conf \
    && ldconfig

WORKDIR /tmp/distribution/src/pam
RUN make clean \
    && make \
    && make install EGA_PAMDIR=/lib/security

WORKDIR /tmp/distribution/src/openssh/src
RUN patch -p1 -N < ../patches/systemd-readyness.patch || true \
    && patch -p1 -N < ../patches/sftp.patch || true \
    && autoreconf \
    && ./configure --prefix=/opt/LocalEGA \
       --with-pam --with-pam-service=ega \
       --with-zlib --with-openssl --with-libedit \
       --with-privsep-user=ega-sshd --with-privsep-path=/run/ega-sshd \
       --without-xauth --without-maildir --without-selinux \
       --with-systemd --with-pid-dir=/run/ega-sshd \
    && make \
    && make install-nosysconf

COPY deploy/docker/distribution/sshd_config /usr/local/share/ega/sshd_config
COPY deploy/docker/distribution/nsswitch.conf /etc/nsswitch.conf
COPY deploy/docker/distribution/pam.sshd /usr/local/share/ega/pam.sshd
COPY src/distribution/src/crypt4gh-fs/fs.conf.sample /usr/local/share/ega/fs.conf.sample
COPY deploy/docker/distribution/entrypoint.sh /usr/local/bin/ega-distribution-entrypoint

RUN chmod +x /usr/local/bin/ega-distribution-entrypoint

ENV PATH="/opt/LocalEGA/bin:/opt/LocalEGA/sbin:${PATH}"

EXPOSE 2223

ENTRYPOINT ["/usr/local/bin/ega-distribution-entrypoint"]
