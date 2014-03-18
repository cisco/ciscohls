# 
# This configure.ac file was created by Matt Snoby as a template and instruction on how
# to use autotools to create a shared library with support for libtool and pkgconfig.
# Any errors found in these template files should be fed back to the author snobym@cisco.com
# so that the template can be updated.

# Template version 1.0
# 4/25/2012    Matt Snoby

#
# Need to add:
#  *support for sysroot in pckconfig
#

#
# Quick How To USE:
# run autoreconf -iv to generate a configure script
# to build for PC host just run ./configure
# to cross compile source your build environment so that your cross compiler, CFLAGS, & LDFLAGS are setup right
# then run configure with the correct HOST arguement such as: ./configure  --host=mipsel-linux
#
# when experimenting with autotools, i have included a small script called "removeall.sh" that will completely
# delete all autotools generated files so that you can be sure you are generating everything.
#


#This is the minimum version of autoconf that is required to execute these scripts
AC_PREREQ([2.61])
#Here we show that the name of the project "example_template", the version "1.0" and the string used for bug reports "example_template_support@cisco.com"
#AC_INIT (package, version, [bug-report], [tarname], [url])
AC_INIT([libgsthls], [1.00.002],[snobym@cisco.com] [Abr], [http://wwwin.cisco.com])

# This will detect the host that is building this software, it will enable us to switch to cross compile mode 
# when necessary
AC_CANONICAL_HOST

# Get system canonical name
# This is necessary for us to cross compile
AC_DEFINE_UNQUOTED(OS, "${host}", [cpu-machine-OS])

#
# If the OS is Linux then enable the GNU system extentions
# This must be done early in the configuration steps.
AC_USE_SYSTEM_EXTENSIONS

#Here in indicate to autoconf that we want automake to autogenerate our makefiles.
#It will also add the -Wall command to the compiler line.
# the Foreign  argument is so that you are not required to have the NEWS, README,AUTHORS and ChangeLog 
# files in the root of your project.
#NOTE: Added the -Wno-extra-portability can be added if you don't want AM_PROG_AR which was added for automake 1.12

AM_INIT_AUTOMAKE([ -Wall parallel-tests silent-rules foreign ])

#This will hide the compiler and linker line by default
# If you want this shown just do a make with the arguement "V=1"
AM_SILENT_RULES([yes])

#This is just used to get some configuration data it's aslo so that the 
#configure script knows the directory on which it is being executed 
#on is actually the project directory
AC_CONFIG_SRCDIR([source/gstciscdemux.c])



#This is an auto generated file that is genreated from configure.ac
#it will hold all the #defines for configuration varibles in this project.
#such things as PACKAGE_BUG_REPORT, PACKAGE_VERSION, and PACKAGE_NAME are contained here.
# the second headerfile foolib.h is a header that will be installed wtih the shared library
# that we are creating
AC_CONFIG_HEADERS([config.h ])
#
# If you don't have CFLAGS set then when you call these AC_PROC_ programs CFLAGS will set with 
# defaults -g -O2 how annoying! 
#
# Checks for programs.

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_AR

#This indicates that we want to use minimum version 2.2 for our libtool
LT_PREREQ([2.2])


#
# Require GSTREAMER versions
#
GST_REQUIRED=0.10.16
GSTPB_REQUIRED=0.10.16


#No static libraries to be created, only shared libraries. Normally the user configures this,
# but since we are almost always going to cross compile static libraries are useless.
LT_INIT([dlopen disable-static ])

# Checks for size of definitions
AC_TYPE_UINT8_T
AC_TYPE_UINT64_T




# this will tell the autoconf system where the m4 Macro directory is
# it's best just to create this directory and have autoconf install
# the necessary scripts into this directory for you.
AC_CONFIG_MACRO_DIR([m4])

# Setup our shared library version along with the API version of the interface
AC_SUBST([GSTHLS_SO_VERSION], [1:0:0])
AC_SUBST([GSTHLS_API_VERSION], [1.0])

#
# Make sure we have pkg-config
PKG_PROG_PKG_CONFIG([0.25])

AS_IF([test -n "$PKG_CONFIG"],[ ],[AC_MSG_ERROR(This build requires pkg-config version "0.25" please install or upgrade it)])



#
# Search for and enable the correct flags to enable pthreads
#
AX_PTHREAD(
 [AC_DEFINE([HAVE_PTHREAD], [1], [Define if you have POSIX threads libraries and header files.])
  LIBS="$PTHREAD_LIBS $LIBS"
  CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
  CXXFLAGS="$CXXFLAGS $PTHREAD_CXXFLAGS"
  CC="$PTHREAD_CC"
  CXX="$PTHREAD_CXX"])



# check for GNU only recursive mutexes
# This is an example so searching for specific functionality
# The variable P_HAS_GNU_MUTEXES will hold the answer in config.h 
P_HAS_GNU_MUTEXES=0
AC_MSG_CHECKING(if gnu enhanced recursive mutexes are available)
AC_TRY_COMPILE(   [#include <pthread.h>],
                  [pthread_mutexattr_t attr;
               pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);],
               P_HAS_GNU_MUTEXES=1)
   if test ${P_HAS_GNU_MUTEXES} = 1; then
      AC_MSG_RESULT(yes)
   else
      AC_MSG_RESULT(no)
   fi
# The following will place define in config.h   
AC_DEFINE_UNQUOTED(P_HAS_GNU_MUTEXES, ${P_HAS_GNU_MUTEXES},[Platform has non POSIX enhanced mutexes])

# Find where libcurl is installed or if it's installed.
#
#AX_LIB_CURL([07.19.4],,[AC_MSG_ERROR([Your system lacks libcurl >= 7.19.4]) ])

LIBCURL_CHECK_CONFIG(,,curl=1, curl=0 )

#
# Begin Start of GSTREAMER code
#
dnl required versions of gstreamer and plugins-base
GST_REQUIRED=0.10.16
GSTPB_REQUIRED=0.10.16
dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-0.10: gstreamer-audio-0.10 >= $GST_REQUIRED
dnl for libgstvideo-0.10: gstreamer-video-0.10 >= $GST_REQUIRED
dnl for libgsttag-0.10: gstreamer-tag-0.10 >= $GST_REQUIRED
dnl for libgstpbutils-0.10: gstreamer-pbutils-0.10 >= $GST_REQUIRED
dnl for libgstfft-0.10: gstreamer-fft-0.10 >= $GST_REQUIRED
dnl for libgstinterfaces-0.10: gstreamer-interfaces-0.10 >= $GST_REQUIRED
dnl for libgstrtp-0.10: gstreamer-rtp-0.10 >= $GST_REQUIRED
dnl for libgstrtsp-0.10: gstreamer-rtsp-0.10 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
  gstreamer-0.10 >= $GST_REQUIRED
  gstreamer-base-0.10 >= $GST_REQUIRED
  gstreamer-controller-0.10 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer0.10-dev and libgstreamer-plugins-base0.10-dev.
      on RPM-based systems gstreamer0.10-devel, libgstreamer0.10-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])
dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-0.10/plugins"
else
  plugindir="\$(libdir)/gstreamer-0.10"
fi
AC_SUBST(plugindir)

#
# End of GSTREAMER requirements
#

#
# DRM requirements
#
dnl set an options flag to force Verimatrix DRM only support
AC_MSG_CHECKING(Enabled Verimatrix support ...)
AC_ARG_ENABLE([force-verimatrix],
[
   AS_HELP_STRING([--enable-force-verimatrix],
      [If set to =yes then we will force Verimatrix only support])
],
[
   AC_MSG_RESULT(yes)
   AC_DEFINE([OPT_FORCE_VERIMATRIX],[1],[Force Verimatrix only])
   verimatrix_support=yes
],
[
   AC_MSG_RESULT(no)
   verimatrix_support=no
])

dnl set an option flag to force Video Gaurd DRM only support
AC_MSG_CHECKING(Enabled VGDRM support ...)
AC_ARG_ENABLE([force-vgdrm],
[
   AS_HELP_STRING([--enable-force-vgdrm],
      [If set to =yes then we will force VGDRM only support])
],
[
   AC_MSG_RESULT(yes)
   AC_DEFINE([OPT_FORCE_VGDRM],[1],[Force VGDRM only])
   vgdrm_support=yes
],
[
   AC_MSG_RESULT(no)
   vgdrm_support=no
])

AS_IF([test "x$verimatrix_support" = "xyes" && test "x$vgdrm_support" = "xyes"],
[
   AC_MSG_ERROR([You can only force one DRM type. Remove --enable-force-verimatrix or --enable-force-vgdrm and re-rerun ./configure])
])

#
# Enf of DRM requirements
#

#
# We MUST have the cisco libHLS installed
#
PKG_CHECK_MODULES([LIBHLS],[libhls >= 1.00.00])
LIBS="$LIBS $LIBHLS_LIBS"
CFLAGS="$CFLAGS $LIBHLS_CFLAGS"
CXXFLAGS="$CXXFLAGS $LIBHLS_CXXFLAGS"


#this tells autoconf what files to generate
# we aare generating the Makefile from Makefile.am
#   board/bcm/Makefile from board/bcm/Makefile.am
#   source/Makefile from source/Makefile.am
#   libFoo-apiversion pkg-cfg file from libFoo.pc.in file in the root
#   
AC_CONFIG_FILES([ Makefile          
                  source/Makefile])
 #3 libAbr-${GSTHLS_API_VERSION}.pc:libAbr.pc])

#
# Use the m4 macro to autogenerate the
# pkgconfig file
#
#AX_CREATE_PKGCONFIG_INFO(,,[$LIBS -lFoo],[Cisco Foo library])
PACKAGE_CFLAGS="$CFLAGS"
PACKAGE_LDFLAGS="$LDFLAGS"
PACKAGE_LIBS="$LIBS"
AX_CREATE_PKGCONFIG_INFO

# Must have AC_OUTPUT to have 
# makefiles created .
AC_OUTPUT

# Here is a nice little print to tell the guy configuring your project
# how things are configured.
echo \
"-------------------------------------------------

 ${PACKAGE_NAME} Version ${PACKAGE_VERSION}

 Prefix: '${prefix}'.
 Compiler: '${CC} ${CFLAGS} '
 Building for debug: '${debugit}'
 CC      : ${CC}
 CPP     : ${CPP}
 CFLAGS  : ${CFLAGS} ${LIBCURL_CFLAGS}   
 CPPFLAGS  : ${CPPFLAGS} $LIBCURL_CPPFLAGS

 LINKER  : $LD $LDFLAGS $LIBS

 LD      : $LD
 LDFLAGS : $LDFLAGS ${LIBCURL}
 LIBS    : $LIBS
 PTHREAD : $PTHREAD_LIBS


 Now type 'make @<:@<target>@:>@'
   where the optional <target> is:
     all                - build all binaries
     install            - install everything
     clean              - removes all built source files.
     distclean          - removes everything configure created

 Notes: To build in Verbose mode execute : make V=1

--------------------------------------------------"

