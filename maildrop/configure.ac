dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 1998 - 2022 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT([maildrop],[3.1.8.20250504],[courier-maildrop@lists.sourceforge.net])

AC_CONFIG_SRCDIR([maildrop.spec.in])
AM_INIT_AUTOMAKE([foreign no-define])

AC_PROG_CC
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
LT_INIT

AC_PATH_PROGS(PERL, perl5 perl, perl)
AC_CHECK_PROG(PCRE2, pcre2-config, yes, no)

if test "$PCRE2" = "no"
then
	AC_MSG_ERROR([pcre2-config was not found, please install PCRE2])
fi

test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'


eval "prefix=$prefix"
eval "exec_prefix=$exec_prefix"
eval "libdir=$libdir"
eval "maildropdir=$libdir/maildrop"

dnl Check whether we should use gdbm, or db.

needs_withdb=0

AC_ARG_WITH(db, [  --with-db=db            Use DB library instead of GDBM.
  --without-db            Disable GDBM extensions],
	db="$withval", db="")

saveLIBS="$LIBS"

case "$db" in
gdbm)
	;;
db)
	;;
no)
	db=""
	;;
"")
	AC_CHECK_LIB(gdbm, gdbm_open)
	AC_CHECK_FUNC(gdbm_open, [ db=gdbm ])
	if test "$db" = ""
	then
		AC_CHECK_LIB(db, dbopen, [ : ],
			AC_CHECK_LIB(db, db_open, [ : ],
			AC_CHECK_LIB(db, db_env_create, [ : ])))

		AC_CHECK_FUNC(dbopen, db="db")
		AC_CHECK_FUNC(db_open, db="db")
		AC_CHECK_FUNC(db_env_create, db="db")
	fi
	AC_MSG_CHECKING(for gdbm or db library)
	AC_MSG_RESULT($db)
	if test "$db" != ""
	then
		ac_configure_args=" --with-db=$db $ac_configure_args"
	fi
	;;
*)
	AC_MSG_ERROR(Invalid --with-db option.)
	;;
esac

LIBS="$saveLIBS"

dblibrary=""

USE_GDBM=0
USE_DB=0
case "$db" in
gdbm)
	AC_CHECK_LIB(gdbm, gdbm_open, [ LIBGDBM=-lgdbm ; LIBS="-lgdbm $LIBS" ])
	AC_CHECK_FUNC(gdbm_open, [ HAVE_GDBM=1 ])
	if test "$HAVE_GDBM" != "1"
	then
		AC_MSG_ERROR(GDBM library not found)
	fi
	dblibrary=libs/gdbmobj/libgdbmobj.la
	USE_GDBM=1
	DBSUBDIR=libs/gdbmobj
	;;
db)
	AC_CHECK_LIB(db, dbopen, [ LIBDB=-ldb ; LIBS="-ldb $LIBS" ],
		AC_CHECK_LIB(db, db_open, [ LIBDB=-ldb ; LIBS="-ldb $LIBS"],
		AC_CHECK_LIB(db, db_env_create,
					[ LIBDB=-ldb; LIBS="-ldb $LIBS"])
		))

	AC_CHECK_FUNC(dbopen, HAVE_BDB=1)
	AC_CHECK_FUNC(db_open, HAVE_BDB=1)
	AC_CHECK_FUNC(db_env_create, HAVE_BDB=1)
	if test "$HAVE_BDB" != "1"
	then
		AC_MSG_ERROR(GDBM library not found)
	fi
	dblibrary=libs/bdbobj/libbdbobj.la
	USE_DB=1
	DBSUBDIR=libs/bdbobj
	;;
*)
	;;
esac

AC_SUBST(USE_GDBM)
AC_SUBST(USE_DB)
AC_SUBST(dblibrary)
AC_SUBST(DBSUBDIR)
AM_CONDITIONAL(HAVE_DBLIBRARY, test "$dblibrary" != "")

LIBS="$saveLIBS"

PKG_CHECK_MODULES(LIBIDN, libidn2 >= 2.0.5, [libidn=yes], [libidn=no])
if test "$libidn" != "yes"
then
	AC_MSG_ERROR([libidn not found])
fi

AX_COURIER_UNICODE_VERSION(2.1)

AC_ARG_WITH(plentyofbeer,
[  --enable-sendmail=prog       set sendmail location
  --enable-trusted-users='...' specify users allowed to use the -d option
  --enable-maildrop-uid=root   UID of maildrop
  --enable-maildrop-gid=mail   GID of maildrop
  --disable-authlib            Disable support for Courier Auth Library
  --enable-use-flock=(1|0)     enable flock()/lockf() locking
  --enable-use-dotlock=(1|0)   enable dotlock locking
  --enable-lockext-def=.lock   set extension for dotlock files
  --enable-locksleep-def=5     set sleep interval dotlock files (seconds)
  --enable-locktimeout-def=60  set timeout for dotlock files (seconds)
  --enable-lockrefresh-def=15  set timeout for dotlock files (seconds)
  --enable-tempdir=.tmp        directory for temp files (in user's home dir)
  --disable-tempdir            disable temp files in home directory (use
                               the tmpfile function)
  --enable-smallmsg=8192       size of buffer for small messages
  --enable-global-timeout=300  global watchdog timer (seconds)
  --enable-crlf-term=0         enable <CR><LF> line termination
  --enable-restrict-trusted=1  restrict -d for trusted users only
  --enable-keep-fromline=(1|0) preserve the original From_ line in messages
  --enable-syslog=1            log fatal errors to syslog
  --enable-authlib-tempreject=(1|0) whether authlib errors cause temprejects]
  --enable-dovecotauth         Enable Dovecot authentication)

AC_ARG_WITH(etcdir,    [  --with-etcdir=dir            Instead of /etc, use this. ],
	[ withetcdir=$withval], [ withetcdir="/etc" ])
AC_SUBST(withetcdir)
AC_DEFINE_UNQUOTED(ETCDIR,"$withetcdir")

AC_ARG_WITH(devel,
[  --with-devel                 install librfc822.a and librfc2045.a],
	devel="$withval")

AM_CONDITIONAL(DEVEL, test "$devel" = "yes")

if test "$devel" = "yes"
then
	DEVEL_LIBS="librfc822.la librfc2045.la"
	DEVEL_INCLUDES="rfc822.h rfc2045.h rfc2047.h"
fi

AC_SUBST(DEVEL_LIBS)
AC_SUBST(DEVEL_INCLUDES)

LIBVERSION_CURRENT=0
LIBVERSION_RELEASE=0
LIBVERSION_AGE=0

LIBVERSION_INFO=$LIBVERSION_CURRENT:$LIBVERSION_RELEASE:$LIBVERSION_AGE
AC_SUBST(LIBVERSION_CURRENT)
AC_SUBST(LIBVERSION_RELEASE)
AC_SUBST(LIBVERSION_AGE)
AC_SUBST(LIBVERSION_INFO)

SOVERSION=0
if test "$LIBVERSION_CURRENT" -ne "$LIBVERSION_AGE"
then
	SOVERSION=`expr $LIBVERSION_CURRENT - $LIBVERSION_AGE`
fi

AC_SUBST(SOVERSION)

AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/libs/docbook)

AC_ARG_WITH(repository, [], REPOSITORY="$withval")
AC_ARG_WITH(package, [], :,
			ac_configure_args=" --with-package=$PACKAGE_NAME $ac_configure_args")
AC_ARG_WITH(version, [], :,
			ac_configure_args=" --with-version=$PACKAGE_VERSION $ac_configure_args")

AC_SUBST(REPOSITORY)

env LC_ALL=C perl -e 'use POSIX qw(strftime); print strftime("DATE=\"%a %b %d %Y\"; DATEFULL=\"%a, %d %b %Y %H:%M:%S %z\"", localtime)' >configure.date
. ./configure.date
rm configure.date
AC_SUBST(DATE)
AC_SUBST(DATEFULL)


AC_CONFIG_SUBDIRS(libs/numlib libs/liblock libs/rfc822 libs/rfc2045 libs/gdbmobj libs/bdbobj libs/makedat libs/maildir libs/maildrop)

AC_CONFIG_FILES([Makefile
	courier-debuild
	dbobj.h
	dbobj.config
	maildrop.spec
	README.html
	INSTALL.html])
AC_OUTPUT
