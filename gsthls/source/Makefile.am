AM_CPPFLAGS = $(LIBCURL_CPPFLAGS)
LDADD = $(LIBCURL)

#
#Static libtool library of the library we
# are creating
plugin_LTLIBRARIES = libgsthls-@GSTHLS_API_VERSION@.la

#
# Source for our library
#
libgsthls_@GSTHLS_API_VERSION@_la_SOURCES=  \
												 gstciscdemux.c			

libgsthls_@GSTHLS_API_VERSION@_la_CPPFLAGS = -I$(top_srcdir)/source/include 
libgsthls_@GSTHLS_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/source/include 
libgsthls_@GSTHLS_API_VERSION@_la_LIBADD = $(GST_LIBS)  
libgsthls_@GSTHLS_API_VERSION@_la_LIBTOOLFLAGS = --tag=disable-static


#
# We now instruct libtool to include teh ABI version information
# into the generated shared library file ( .so)
# The ABI is defined in the top level configure.ac file
#
libgsthls_@GSTHLS_API_VERSION@_la_LDFLAGS = -version-info $(GSTHLS_SO_VERSION) ${LIBCURL} $(GST_PLUGIN_LDFLAGS) 



# 
# Here is the list of public header files and where they will be installed too.
# This is for the install / pkg-cfg
# IF you had a include directory structure you use nobase_ as the prefix so that
# the directory structure will be used when doing an install.
#
#This defines the include directory name that we are creating once we do an install
libgsthls_includedir = $(includedir)/gstreamer-0.10/gst/libgsthls-$(GSTHLS_API_VERSION)

#This is the list of header files to be installed in the directory that was created on the target
# libgsthls_includedir from above.
nobase_libgsthls_include_HEADERS = gstciscdemux.h



