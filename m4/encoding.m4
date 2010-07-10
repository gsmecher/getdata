dnl (C) 2008-2010 D. V. Wiebe
dnl
dnl llllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllll
dnl
dnl This file is part of the GetData project.
dnl
dnl GetData is free software; you can redistribute it and/or modify it under
dnl the terms of the GNU Lesser General Public License as published by the
dnl Free Software Foundation; either version 2.1 of the License, or (at your
dnl option) any later version.
dnl
dnl GetData is distributed in the hope that it will be useful, but WITHOUT
dnl ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
dnl FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
dnl License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public License
dnl along with GetData; if not, write to the Free Software Foundation, Inc.,
dnl 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

dnl GD_CHECK_ENCODING
dnl -------------------------------------------------------------
dnl Run a bunch of checks to see if we can build and test encodings
dnl based on an external library.  This function is easily broken.
AC_DEFUN([GD_CHECK_ENCODING],
[
have_this_header=
have_this_lib=
AC_ARG_WITH([lib$2], AS_HELP_STRING([--with-lib$2=PREFIX],
            [use the lib$2 installed in PREFIX [[autodetect]]]),
            [
             case "${withval}" in
               no) use_$1="no" ;;
               yes) use_$1="yes"; $1_prefix= ;;
               *) use_$1="yes"; $1_prefix="${withval}" ;;
             esac
             ], [ use_$1="yes"; $1_prefix=; ])
m4_divert_once([HELP_WITH], AS_HELP_STRING([--without-lib$2],
            [disable encodings supported by lib$2, even if the library is present]))

if test "x$no_extern" = "xyes"; then
  use_$1="no";
fi

if test "x$use_$1" = "xyes"; then
  dnl search for library
  echo
  echo "*** Configuring $1 support"
  echo
  saved_ldflags=$LDFLAGS
  saved_libs=$LIBS
  if test "x$[]$1_prefix" != "x"; then
    LDFLAGS="$LDFLAGS -L$[]$1_prefix/lib"
  fi
  AC_CHECK_LIB([$2],[$3],[have_this_lib=yes]
  AC_DEFINE(AS_TR_CPP([HAVE_LIB$2]), 1,
    [Define to 1 if you have the `$2' library (-l$2).]))
  LDFLAGS=$saved_ldflags
  LIBS=$saved_libs

dnl search for header
  saved_cppflags=$CPPFLAGS
  if test "x$[]$1_prefix" != "x"; then
    CPPFLAGS="$CPPFLAGS -I$[]$1_prefix/include"
  fi
  AC_CHECK_HEADERS([$4],[have_this_header=yes])
  CPPFLAGS=$saved_cppflags
fi

dnl cleanup
AS_TR_CPP([$1_CPPFLAGS])=
AS_TR_CPP([$1_LDFLAGS])=
if test "x$have_this_header" = "xyes" -a "x$have_this_lib" = "xyes"; then
  if test "x$[]$1_prefix" = "x"; then
    AS_TR_CPP([$1_LDFLAGS])="-l$2"
    AS_TR_CPP([$1_SEARCHPATH])="$PATH"
  else 
    AS_TR_CPP([$1_CPPFLAGS])="-I$[]$1_prefix/include"
    AS_TR_CPP([$1_LDFLAGS])="-L$[]$1_prefix/lib -l$2"
    AS_TR_CPP([$1_SEARCHPATH])="$[]$1_prefix/bin:$PATH"
  fi
  AC_DEFINE(AS_TR_CPP([USE_$1]), [], [ Define to enable $1 support ])
else
  use_$1="no";
  AS_TR_CPP([$1_SEARCHPATH])="$PATH"
fi
AC_SUBST(AS_TR_CPP([$1_CPPFLAGS]))
AC_SUBST(AS_TR_CPP([$1_LDFLAGS]))

dnl executables needed for tests
AC_PATH_PROGS([path_$5], [$5], [not found], [$AS_TR_CPP([$1_SEARCHPATH])])

if test "x$path_$5" != "xnot found"; then
  AC_DEFINE_UNQUOTED(AS_TR_CPP([$5]), ["$path_$5"],
                     [ Define to the full path to the `$5' binary ])
fi

ifelse(`x$6', `x',,[
AC_PATH_PROGS([path_$6], [$6], [not found], [$AS_TR_CPP([$1_SEARCHPATH])])

if test "x$path_$6" != "xnot found"; then
  AC_DEFINE_UNQUOTED(AS_TR_CPP([$6]), ["$path_$6"],
                     [ Define to the full path to the `$6' binary ])
fi
$7
])
AM_CONDITIONAL(AS_TR_CPP([USE_$1]), [test "x$use_$1" = "xyes"])
AM_CONDITIONAL(AS_TR_CPP([TEST_$1]),
              [test "x$path_$5" != "xnot found" -a "x$path_$6" != "xnot found"])

dnl add to summary
if test "x$use_$1" != "xno"; then
  if test "x$use_modules" != "xno"; then
    ENCODINGS_MODS="${ENCODINGS_MODS} $1";
  else
    ENCODINGS_BUILT="${ENCODINGS_BUILT} $1";
  fi
else
  ENCODINGS_LEFT="${ENCODINGS_LEFT} $1";
fi
])
