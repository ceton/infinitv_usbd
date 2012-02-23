AC_PREREQ([2.63])
AC_INIT([infinitv_usbd], [0.1], [linux@cetoncorp.com])
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

m4-ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES(Makefile
                src/Makefile)

AC_PROG_CC

PKG_CHECK_MODULES(GUSB, gusb)

AC_CHECK_HEADERS(stdlib.h)

AC_OUTPUT
