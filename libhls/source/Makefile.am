AM_CPPFLAGS = $(LIBCURL_CPPFLAGS)
LDADD = $(LIBCURL)

#
#Static libtool library of the library we
# are creating
lib_LTLIBRARIES = libHls-@HLS_API_VERSION@.la

#
# Source for our library
#
libHls_@HLS_API_VERSION@_la_SOURCES= curlUtils.c               \
												 hlsDownloader.c 	         \
												 hlsDownloaderUtils.c 	   \
												 hlsPlayerInterface.c	   \
												 hlsPlaybackController.c   \
												 hlsPlugin.c 					\
												 hlsSession.c 					\
												 hlsSessionUtils.c 			\
												 m3u8Parser.c 					\
												 m3u8ParseUtils.c 		   \
												 llUtils.c						

libHls_@HLS_API_VERSION@_la_CPPFLAGS = -I$(top_srcdir)/source/include 
libHls_@HLS_API_VERSION@_la_CFLAGS = -I$(top_srcdir)/source/include 
libHls_@HLS_API_VERSION@_la_LIBADD =   
libHls_@HLS_API_VERSION@_la_LIBTOOLFLAGS = --tag=disable-static


#
# We now instruct libtool to include teh ABI version information
# into the generated shared library file ( .so)
# The ABI is defined in the top level configure.ac file
#
libHls_@HLS_API_VERSION@_la_LDFLAGS = -version-info $(HLS_SO_VERSION) ${LIBCURL}  



# 
# Here is the list of public header files and where they will be installed too.
# This is for the install / pkg-cfg
# IF you had a include directory structure you use nobase_ as the prefix so that
# the directory structure will be used when doing an install.
#
#This defines the include directory name that we are creating once we do an install
libHls_includedir = $(includedir)/libHls-$(HLS_API_VERSION)

#This is the list of header files to be installed in the directory that was created on the target
# libHls_includedir from above.
nobase_libHls_include_HEADERS =  include/hlsPlugin.h include/sourcePlugin.h include/llUtils.h 



