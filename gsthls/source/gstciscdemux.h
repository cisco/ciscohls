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


#ifndef __GST_CISCDEMUX_H__
#define __GST_CISCDEMUX_H__

#include <gst/gst.h>
#include "sourcePlugin.h"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_CISCDEMUX \
  (gst_ciscdemux_get_type())
#define GST_CISCDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CISCDEMUX,Gstciscdemux))
#define GST_CISCDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CISCDEMUX,GstciscdemuxClass))
#define GST_IS_CISCDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CISCDEMUX))
#define GST_IS_CISCDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CISCDEMUX))

typedef struct _Gstciscdemux      Gstciscdemux;
typedef struct _GstciscdemuxClass GstciscdemuxClass;
typedef struct
{
   GstBuffer *buf ;
#if GST_CHECK_VERSION(1,0,0)
   GstMapInfo info;
#endif
   unsigned char *memory;
   size_t   size;
   int      bInUse;

}tMemoryStruct;

typedef struct
{
   int bInUse;
   srcSessionId_t pSessionID;
   void           *pHandle_Player;
   char           *pUrl;

   unsigned int   GstBufferSize;
 //  GstAdapter     *pAdapter;

}tSession;


struct _Gstciscdemux
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  /* Array of discrete source pads */
  GstPad **srcpad_discrete;

  GstPad *downstream_peer_pad;

  gchar    *LicenseID;
  gchar    *drmType;
  /* Array of src pad caps */
  GstCaps **inputStreamCap;

  gboolean  silent;
  gboolean  capsSet;
  gchar     *uri;
  tSession  *pCscoHlsSession ;
  srcPluginFunc_t   HLS_pluginTable;
  playerEvtCallback_t playerEvtCb;

  GThread         *getPTSThread;
  GCond           PTSThreadCond;
  GMutex          PTSMutex;
  gboolean        bKillPTSThread;
  gboolean        bGetPTSThreadRunning;
  gfloat          speed;
  guint           numSrcPadsActive;
  gboolean        bDisableMainStreamAudio;
  gint64          bufferPts;
  gint64          seekpos;
  gboolean        newSegment;
  gchar           defaultAudioLangISOCode[ISO_LANG_CODE_LEN + 1];
};

struct _GstciscdemuxClass
{
  GstElementClass parent_class;
};

GType gst_ciscdemux_get_type (void);

G_END_DECLS

#endif /* __GST_CISCDEMUX_H__ */
