/*
    GSTHLS
    Copyright (C) {2015}  {Cisco System}

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA

    Contributing Authors: Matt Snoby, Tankut Akgul, Saravanakumar Periyaswamy

*/
#ifndef __GST_M3U8_H__
#define __GST_M3U8_H__

#include <gst/gst.h>
/*#include <gst/base/gstbasesrc.h>*/
#include <sys/socket.h>
#include <netdb.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_M3U8 \
  (gst_m3u8_get_type())
#define GST_M3U8(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_M3U8,GstM3U8))
#define GST_M3U8_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_M3U8,GstM3U8Class))
#define GST_IS_M3U8(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_M3U8))
#define GST_IS_M3U8_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_M3U8))

typedef struct _GstM3U8      GstM3U8;
typedef struct _GstM3U8Class GstM3U8Class;

struct _GstM3U8
{
   /*GstBaseSrc element;*/
   GstElement parent;

   /* Private data */
   gchar    *uri;

   GstPad   *sinkPad;
   GstCaps  *inputCaps;

   int       capsSet;


   GstPad   *srcPad;
   GstCaps  *srcOutputCaps;   /*Advertises what our output capabilities are*/

   GstPad   *fetcherpad;


   GstAdapter  *pAdapter;

   CURL     *curl_handle;
   gint     hSocket;
   gboolean pastHeader;
   gboolean liveStream;

   int c;
};

struct _GstM3U8Class
{
  /* GstBaseSrcClass parent_class; */
   GstElementClass   parent_class;
};

GType gst_m3u8_get_type (void);

G_END_DECLS

#endif /* __GST_M3U8_H__ */
