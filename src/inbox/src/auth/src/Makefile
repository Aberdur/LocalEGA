#
# Makefile for the NSS and PAM modules used in Local EGA
#
# Blowfish code from http://www.openwall.com/crypt/
#

NSS_LD_SONAME=-Wl,-soname,libnss_ega.so.2
NSS_LIBRARY=libnss_ega.so.2.0
PAM_AUTH_LIBRARY = pam_ega_auth.so
PAM_ACCT_LIBRARY = pam_ega_acct.so
PAM_SESSION_LIBRARY = pam_ega_session.so
KEYS_EXEC = ega_ssh_keys

CC=gcc
LD=ld
AS=gcc -c
CFLAGS=-Wall -Werror -Wstrict-prototypes -fPIC -I. -I/usr/local/include -O2
LIBS=-lpam -lcurl -lsqlite3

ifdef SYSLOG
CFLAGS += -DHAS_SYSLOG
endif

EGA_LIBDIR=/usr/local/lib/ega
EGA_BINDIR=/usr/local/bin
EGA_PAMDIR=/lib/security

HEADERS = utils.h config.h cache.h json.h cega.h $(wildcard jsmn/*.h) $(wildcard blowfish/*.h)

NSS_SOURCES = nss.c config.c cache.c json.c cega.c $(wildcard jsmn/*.c)
NSS_OBJECTS = $(NSS_SOURCES:%.c=%.o)

PAM_AUTH_SOURCES = pam_auth.c $(wildcard blowfish/*.c)
PAM_AUTH_OBJECTS = $(PAM_AUTH_SOURCES:%.c=%.o) blowfish/x86.o

PAM_SESSION_OBJECTS = pam_session.o

PAM_ACCT_OBJECTS = pam_acct.o

KEYS_SOURCES = keys.c config.c cache.c json.c cega.c $(wildcard jsmn/*.c)
KEYS_OBJECTS = $(KEYS_SOURCES:%.c=%.o)

.PHONY: all debug clean install install-nss install-pam
.SUFFIXES: .c .o .S .so .so.2 .so.2.0

all: install

debug1: CFLAGS += -DDEBUG=1 -g -DREPORT
debug1: install

debug2: CFLAGS += -DDEBUG=2 -g -DREPORT
debug2: install

debug3: CFLAGS += -DDEBUG=3 -g -DREPORT
debug3: install

$(EGA_LIBDIR) $(EGA_BINDIR) $(EGA_PAMDIR):
	@echo "Creating dir: $@"
	install -d $@


$(NSS_LIBRARY): $(HEADERS) $(NSS_OBJECTS)
	@echo "Linking objects into $@"
	@$(CC) -shared $(NSS_LD_SONAME) -o $@ $(NSS_OBJECTS) -lcurl -lsqlite3

$(PAM_AUTH_LIBRARY): $(PAM_AUTH_OBJECTS)
	@echo "Linking objects into $@"
	@$(LD) -x --shared -o $@ $(PAM_AUTH_OBJECTS) -lpam

$(PAM_ACCT_LIBRARY): $(PAM_ACCT_OBJECTS)
	@echo "Linking objects into $@"
	@$(LD) -x --shared -o $@ $(PAM_ACCT_OBJECTS) -lpam

$(PAM_SESSION_LIBRARY): $(PAM_SESSION_OBJECTS)
	@echo "Linking objects into $@"
	@$(LD) -x --shared -o $@ $(PAM_SESSION_OBJECTS) -lpam

$(KEYS_EXEC): $(HEADERS) $(KEYS_OBJECTS) 
	@echo "Creating $@"
	@$(CC) -o $@ $(KEYS_OBJECTS) -lcurl -lsqlite3

blowfish/x86.o: blowfish/x86.S
	@echo "Compiling $<"
	@$(AS) -o $@ $<

%.o: %.c
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

install-nss: $(NSS_LIBRARY)
	@echo "Installing $< into $(EGA_LIBDIR)"
	@install $< $(EGA_LIBDIR)

install-pam-auth: $(PAM_AUTH_LIBRARY) | $(EGA_PAMDIR)
	@echo "Installing $< into $(EGA_PAMDIR)"
	@install $< $(EGA_PAMDIR)

install-pam-acct: $(PAM_ACCT_LIBRARY) | $(EGA_PAMDIR)
	@echo "Installing $< into $(EGA_PAMDIR)"
	@install $< $(EGA_PAMDIR)

install-pam-session: $(PAM_SESSION_LIBRARY) | $(EGA_PAMDIR)
	@echo "Installing $< into $(EGA_PAMDIR)"
	@install $< $(EGA_PAMDIR)

install-pam: install-pam-auth install-pam-acct install-pam-session

install-keys: $(KEYS_EXEC) | $(EGA_BINDIR)
	@echo "Installing $< into $(EGA_BINDIR)"
	@install -m 700 $< $(EGA_BINDIR)

install: install-nss install-pam install-keys
	@echo "Do not forget to run ldconfig and create/configure the file /etc/ega/auth.conf"
	@echo "Look at the auth.conf.sample here, for example"

clean:
	-rm -f $(NSS_LIBRARY) $(NSS_OBJECTS)
	-rm -f $(PAM_AUTH_LIBRARY) $(PAM_AUTH_OBJECTS)
	-rm -f $(PAM_ACCT_LIBRARY) $(PAM_ACCT_OBJECTS)
	-rm -f $(PAM_SESSION_LIBRARY) $(PAM_SESSION_OBJECTS)
	-rm -f $(KEYS_EXEC) $(KEYS_OBJECTS)
