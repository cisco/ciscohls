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
AC_INIT([libhls], [1.00.00],[snobym@cisco.com] [Hls], [http://wwwin.cisco.com])

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
AC_CONFIG_SRCDIR([source/hlsPlugin.c])



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
AC_SUBST([HLS_SO_VERSION], [1:0:0])
AC_SUBST([HLS_API_VERSION], [1.0])

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

#
# Find where libcurl is installed or if it's installed.
#

LIBCURL_CHECK_CONFIG(,,curl=1, curl=0 )

#
# We need the math lib specifically floorf
#
AC_CHECK_LIB(m,floorf,[bFoundFloorf=1],[bFoundFloorf=0])
AC_MSG_CHECKING([Checking for floorf in the math library])
if test $bFoundFloorf == 1
then
   AC_MSG_RESULT([yes])
   LDFLAGS="-lm $LDFLAGS"
else
   AC_MSG_RESULT([no])
   AC_MSG_ERROR([Your math library is missing floorf])
fi


#this tells autoconf what files to generate
# we aare generating the Makefile from Makefile.am
#   board/bcm/Makefile from board/bcm/Makefile.am
#   source/Makefile from source/Makefile.am
#   libFoo-apiversion pkg-cfg file from libFoo.pc.in file in the root
#   
AC_CONFIG_FILES([ Makefile          
                  source/Makefile
                  libhls.pc:libhls.pc.in])


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

