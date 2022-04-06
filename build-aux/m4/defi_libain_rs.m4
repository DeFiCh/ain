AC_DEFUN([DEFI_LIBAIN_RS],
[
AC_ARG_WITH([libain-rs],
  [AS_HELP_STRING([--with-libain-rs@<:@=ARG@:>@],
    [use libain-rs library from a standard location (ARG=yes),
     from the specified location (ARG=<path>),
     or disable it (ARG=no)
     @<:@ARG=yes@:>@ ])],
    [
     AS_CASE([$withval],
       [no],[use_libain_rs="no";LIBAIN_RS_PKG_PATH=""],
       [yes],[use_libain_rs="yes";LIBAIN_RS_PKG_PATH=""],
       [use_libain_rs="yes";LIBAIN_RS_PKG_PATH="$withval"])
    ],
    [use_libain_rs="yes"])

    if test "$LIBAIN_RS_PKG_PATH" != ""; then
        LIBAIN_RS_LDFLAGS="-L$LIBAIN_RS_PKG_PATH/lib"
        LIBAIN_RS_CPPFLAGS="-I$LIBAIN_RS_PKG_PATH/include"
    fi
    LDFLAGS="$LIBAIN_RS_LDFLAGS $LDFLAGS"
    export LDFLAGS
    CPPFLAGS="$CPPFLAGS $LIBAIN_RS_CPPFLAGS"
    export CPPFLAGS

    if test x$use_libain_rs != xno; then
        LIBAIN_RS_LIBS=-lruntime
        AC_SUBST(LIBAIN_RS_LIBS)
    fi
])
