m4_define([infinitv_usbd_major_version], [0])
m4_define([infinitv_usbd_minor_version], [1])
m4_define([infinitv_usbd_micro_version], [0])
m4_define([infinitv_usbd_version],
          [infinitv_usbd_major_version.infinitv_usbd_minor_version.infinitv_usbd_micro_version])

AC_PREREQ([2.63])
AC_INIT([infinitv-usbd], [infinitv_usbd_version], [linux@cetoncorp.com])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE()
m4-ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES(Makefile
                src/Makefile)

LT_PREREQ([2.2.6])
LT_INIT()

AC_PROG_CC

PKG_CHECK_MODULES(GIO, gio-2.0)
PKG_CHECK_MODULES(LIBNL, libnl-1)
PKG_CHECK_MODULES(GUSB, gusb)

AC_CHECK_HEADERS(stdlib.h)

AC_OUTPUT
