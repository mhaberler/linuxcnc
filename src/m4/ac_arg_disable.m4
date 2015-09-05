dnl ================================
dnl        AC_ARG_DISABLE(foo..)
dnl   usage:   same as AC_ARG_ENABLE
dnl ================================
AC_DEFUN([AC_ARG_DISABLE], [
    disablestring=`echo "$1" | sed 's/_/-/g'`
    AC_MSG_CHECKING([--disable-$disablestring])
    AC_ARG_ENABLE($1, [$2],
    [
        if test "$enableval" = yes; then
            ac_cv_use_$1='$3=yes'
        else
            ac_cv_use_$1='$3=no'
        fi
    ],
    [
        ac_cv_use_$1='$3='$DEFAULT_$3
    ])

    eval "$ac_cv_use_$1"

    if test "$$3" = "no"; then
        AC_MSG_RESULT(yes)
    else
        AC_MSG_RESULT(no)
    fi
])
