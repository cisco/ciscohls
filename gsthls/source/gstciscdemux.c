/*
* This gstreamer plugin was written for the cisco HLS library.  It will handle auto plugging
* and with the correct elements downstream can decrypt DRM encrypted HLS streams.
*/

/**
* SECTION:element-cischlsdemux
*
* FIXME:Describe cischlsdemux here.
*
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <time.h>

#include <gst/gst.h>
#include <gst/base/gsttypefindhelper.h>

#include "gstciscdemux.h"
#include "string.h"

GST_DEBUG_CATEGORY_STATIC (gst_ciscdemux_debug);
#define GST_CAT_DEFAULT gst_ciscdemux_debug
#define DRM_TYPE_VERIMATRIX "ENCRYPTED_VERIMATRIX_HLS"
#define DRM_TYPE_VGDRM "ENCRYPTED_VGDRM_HLS"
#define DRM_TYPE_BASIC "ENCRYPTED_BASIC_HLS"

/* demux signals and args */
enum
{
   /* FILL ME */
   LAST_SIGNAL
};

enum
{
   PROP_0,
   PROP_SILENT
};

/* the capabilities of the inputs and outputs.
*
* describe the real formats here.
*/
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS ("application/x-hlss"));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_discrete_factory = GST_STATIC_PAD_TEMPLATE ("src_discrete_%02d",
                                                                            GST_PAD_SRC,
                                                                            GST_PAD_SOMETIMES,
                                                                            GST_STATIC_CAPS_ANY );

#if GST_CHECK_VERSION(1,0,0)
#define gst_ciscdemux_parent_class parent_class
G_DEFINE_TYPE (Gstciscdemux, gst_ciscdemux, GST_TYPE_ELEMENT);
#else
GST_BOILERPLATE (Gstciscdemux, gst_ciscdemux, GstElement, GST_TYPE_ELEMENT);
#endif

static void gst_ciscdemux_set_property (GObject * object, guint prop_id,
                                        const GValue * value, GParamSpec * pspec);
static void gst_ciscdemux_get_property (GObject * object, guint prop_id,
                                        GValue * value, GParamSpec * pspec);

#if GST_CHECK_VERSION(1,0,0)
static GstFlowReturn gst_ciscdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static GstPadLinkReturn gst_ciscdemux_src_link (GstPad * pad, GstObject * parent, GstPad * peer);
static gboolean gst_cscohlsdemuxer_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
#else
static GstFlowReturn gst_ciscdemux_chain (GstPad * pad, GstBuffer * buf);
static GstPadLinkReturn gst_ciscdemux_src_link(GstPad *srcPad, GstPad *peerPad);
static gboolean gst_cscohlsdemuxer_sink_event (GstPad * pad, GstEvent * event);
#endif
static GstStateChangeReturn gst_cscohlsdemuxer_change_state (GstElement * element, GstStateChange transition);

static GstClockTime gst_cisco_hls_get_duration (Gstciscdemux *demux);
static gboolean cisco_hls_initialize (Gstciscdemux *demux);
static gboolean cisco_hls_open(Gstciscdemux *demux, char *pPlaylistUri);
static gboolean cisco_hls_start(Gstciscdemux *demux);
static gboolean cisco_hls_pause(Gstciscdemux *demux);
static gboolean cisco_hls_close(Gstciscdemux *demux);
static gboolean cisco_hls_finalize(Gstciscdemux *demux);
static gboolean gst_cisco_hls_seek (Gstciscdemux *demux, GstEvent *event);
static void * getCurrentPTSNotify(void *data);
static gboolean gst_ciscdemux_get_caps(srcBufferMetadata_t *metadata,
                                       GstBuffer *buf,
                                       GstCaps **caps);
static gboolean gst_ciscdemux_flush(Gstciscdemux *demux, GstPad *srcpad);
static gboolean gst_ciscdemux_disable_main_stream_audio(Gstciscdemux *demux);
static gboolean gst_ciscdemux_send_eos(GstPad *srcpad);
static gboolean gst_ciscdemux_send_low_delay_videomask_event(Gstciscdemux *demux, gboolean enable);
static gboolean gst_ciscdemux_send_event_to_all_srcpads(Gstciscdemux *demux, GstEvent *event);
static gboolean gst_ciscdemux_send_flush_to_all_srcpads(Gstciscdemux *demux);
/* GObject vmethod implementations */

/* These global variables must be removed*/
//TODO FIX
static GstStaticCaps abr_caps = GST_STATIC_CAPS ("application/x-hlss");
#define DATA_SCAN_CTX_CHUNK_SIZE 4096

typedef struct
{
   guint64 offset;
   const guint8 *data;
   gint size;
} DataScanCtx;

static inline void
data_scan_ctx_advance (GstTypeFind * tf, DataScanCtx * c, guint bytes_to_skip)
{
   c->offset += bytes_to_skip;
   if (G_LIKELY (c->size > bytes_to_skip)) {
      c->size -= bytes_to_skip;
      c->data += bytes_to_skip;
   } else {
      c->data += c->size;
      c->size = 0;
   }
}

static inline gboolean
data_scan_ctx_ensure_data (GstTypeFind * tf, DataScanCtx * c, gint min_len)
{
   const guint8 *data;
   guint64 len;
   guint chunk_len = MAX (DATA_SCAN_CTX_CHUNK_SIZE, min_len);

   if (G_LIKELY (c->size >= min_len))
      return TRUE;

   data = gst_type_find_peek (tf, c->offset, chunk_len);
   if (G_LIKELY (data != NULL)) {
      c->data = data;
      c->size = chunk_len;
      return TRUE;
   }

   /* if there's less than our chunk size, try to get as much as we can, but
   * always at least min_len bytes (we might be typefinding the first buffer
   * of the stream and not have as much data available as we'd like) */
   len = gst_type_find_get_length (tf);
   if (len > 0) {
      len = CLAMP (len - c->offset, min_len, chunk_len);
   } else {
      len = min_len;
   }

   data = gst_type_find_peek (tf, c->offset, len);
   if (data != NULL) {
      c->data = data;
      c->size = len;
      return TRUE;
   }

   return FALSE;
}
#define ABR_CAPS gst_static_caps_get (&abr_caps)
static void hls_type_find (GstTypeFind * tf, gpointer unused)
{
   DataScanCtx c = { 0, NULL, 0 };

   if (G_UNLIKELY (!data_scan_ctx_ensure_data (tf, &c, 7)))
      return;

   if (memcmp (c.data, "#EXTM3U", 7))
      return;

   data_scan_ctx_advance (tf, &c, 7);

   /* Check only the first 256 bytes */
   while (c.offset < 1024) {
      if (G_UNLIKELY (!data_scan_ctx_ensure_data (tf, &c, 21)))
         return;

      /* Search for # comment lines */
      if (c.data[0] == '#' && (memcmp (c.data, "#EXT-X-TARGETDURATION", 21) == 0
         || memcmp (c.data, "#EXT-X-STREAM-INF", 17) == 0)) {
            gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, ABR_CAPS);
            return;
      }

      data_scan_ctx_advance (tf, &c, 1);
   }
}
static GstEvent* gst_ciscdemux_create_decryption_event(  srcBufferMetadata_t* metadata,  Gstciscdemux *demux) 
{
   GstEvent       *event = NULL;
   GstStructure   *structure = NULL;       
   char  strKey[64];
   char  strIv[64];
   int   i;
   int   base;
   char  *p;
   char  *s;

   memset(strKey, 0, 64);
   memset(strIv, 0, 64);

   // make sure that this is indeed an encryption type we understand.
   if (metadata)
   {
      //This buffer has meta data, need to see if it's encrypted
      //TODO: This will take a lot of research but instead of converting the 
      //key and iv array from binary to string, just send a byte array down the pipline.
      //
      if (metadata->encType == SRC_ENC_AES128_CBC)
      {

         p = strKey;
         s = (char*) metadata->key;
         // each unsigned char is 2 characters.

         for (i =0; i< 16; i++)
         {
            base = g_sprintf(p, "%02x", (s[i]& 0xFF));
            p += base;
         }


         p = strIv;
         s = (char*) metadata->iv;
         // each unsigned char is 2 characters.

         for (i =0; i< 16; i++)
         {
            base = g_sprintf(p, "%02x", (s[i] & 0xFF));
            p += base;

         }

#ifdef OPT_FORCE_VERIMATRIX
         char * drmType = DRM_TYPE_VERIMATRIX;
         GST_LOG("Forcing DRM type to: \"%s\"\n", drmType);
         structure = gst_structure_new(drmType,
            "keyURI", G_TYPE_STRING, metadata->keyURI,
            "iv",     G_TYPE_STRING, strIv,
            NULL);
#elif OPT_FORCE_VGDRM
         char * drmType = DRM_TYPE_VGDRM;
         GST_LOG("Forcing DRM type to: \"%s\"\n", drmType);
         structure = gst_structure_new(drmType,
            "keyURI",   G_TYPE_STRING, metadata->keyURI,
            "iv",       G_TYPE_STRING, strIv,
            "LicenseID",G_TYPE_STRING, demux->LicenseID,
            NULL);
#else
         char * drmType = DRM_TYPE_BASIC;
         structure = gst_structure_new(drmType,
            "keyURI", G_TYPE_STRING, metadata->keyURI,
            "iv",     G_TYPE_STRING, strIv,
            NULL);
#endif
         if (structure == NULL)
         {
            GST_ERROR("Error creating event message\n");
         }
         else
         {
            GST_LOG("sending out DRM info: %s \n", gst_structure_get_name(structure));
            event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
         }
      }
   }
   return event;
}

#if !GST_CHECK_VERSION(1,0,0)
static void
gst_ciscdemux_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Cisco HLS Demuxer",
    "Demuxer/Fetcher",
    "Cisco's HLS library GStreamer interface",
    "Matt Snoby <<snobym@cisco.com>>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_discrete_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}
#endif
/* initialize the ciscdemux's class */
static void
gst_ciscdemux_class_init (GstciscdemuxClass * klass)
{
   GObjectClass *gobject_class;
   GstElementClass *gstelement_class;

   gobject_class = (GObjectClass *) klass;
   gstelement_class = (GstElementClass *) klass;

   gobject_class->set_property = gst_ciscdemux_set_property;
   gobject_class->get_property = gst_ciscdemux_get_property;

   g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
      FALSE, G_PARAM_READWRITE));

   gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cscohlsdemuxer_change_state);

#if GST_CHECK_VERSION(1,0,0)
   gst_element_class_set_details_simple(gstelement_class,
      "Cisco HLS Demuxer",
      "Demuxer/Fetcher",
      "Cisco's HLS library GStreamer interface",
      "Matt Snoby <<snobym@cisco.com>>"
      "Saravanakumar Periyaswamy <<sarperiy@cisco.com>>"
      "Tankut Akgul <<akgult@cisco.com>>");
   gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
   gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_discrete_factory));
   gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
#endif
}

/* initialize the new element
* instantiate pads and add them to element
* set pad calback functions
* initialize instance structure
*/
static void
#if GST_CHECK_VERSION(1,0,0)
gst_ciscdemux_init (Gstciscdemux * demux)
#else
gst_ciscdemux_init (Gstciscdemux * demux, GstciscdemuxClass * gclass)
#endif
{

   /* sink pad*/
   demux->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
   demux->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
   /* Sink pad chain function set*/
   gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR(gst_ciscdemux_chain));

   gst_pad_set_event_function (demux->sinkpad, GST_DEBUG_FUNCPTR (gst_cscohlsdemuxer_sink_event));

   gst_pad_set_link_function(demux->srcpad, GST_DEBUG_FUNCPTR (gst_ciscdemux_src_link));

   /* now enable the pads*/
   gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
   gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad);

   demux->silent = FALSE;
   demux->capsSet =0;
   demux->srcpad_discrete = NULL;
   demux->numSrcPadsActive = 0;
   demux->bKillPTSThread = FALSE;
   demux->playerEvtCb = NULL;
   demux->downstream_peer_pad = NULL;
   demux->uri = NULL;
   demux->bGetPTSThreadRunning = FALSE;
   demux->speed = 0.0;
   demux->bDisableMainStreamAudio = FALSE;
   demux->bufferPts = INVALID_PTS;
   demux->isFlushOnSeek = FALSE;

   if(0 != pthread_mutex_init(&demux->PTSMutex, NULL))
   {
      GST_WARNING("Failed to init PTSMutex\n");
   }

   if(0 != pthread_cond_init(&demux->PTSThreadCond, NULL))
   {
      GST_WARNING("Failed to init PTSThreadCond\n");
   }
}

static void
gst_ciscdemux_set_property (GObject * object, guint prop_id,
                            const GValue * value, GParamSpec * pspec)
{
   Gstciscdemux *demux = GST_CISCDEMUX (object);

   switch (prop_id) {
       case PROP_SILENT:
          demux->silent = g_value_get_boolean (value);
          break;
       default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
   }
}

static void
gst_ciscdemux_get_property (GObject * object, guint prop_id,
                            GValue * value, GParamSpec * pspec)
{
   Gstciscdemux *demux = GST_CISCDEMUX (object);

   switch (prop_id) {
       case PROP_SILENT:
          g_value_set_boolean (value, demux->silent);
          break;
       default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
   }
}


/* chain function
* this function does the actual processing
* This is actually not used at all it just dumps the
* packet.  The HLS library actually pulls the m3u8 file.
*/
static GstFlowReturn
#if GST_CHECK_VERSION(1,0,0)
gst_ciscdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
#else
gst_ciscdemux_chain (GstPad * pad, GstBuffer * buf)
#endif
{
   Gstciscdemux *demux;
#if GST_CHECK_VERSION(1,0,0)
   demux = GST_CISCDEMUX (parent);
#else
   demux = GST_CISCDEMUX (GST_OBJECT_PARENT (pad));
#endif
   gst_buffer_unref(buf);
   /* just push out the incoming buffer without touching it */
   return GST_FLOW_OK;
}

#if GST_CHECK_VERSION(1,0,0)
static GstPadLinkReturn gst_ciscdemux_src_link (GstPad * pad, GstObject * parent, GstPad * peer)
#else
static GstPadLinkReturn gst_ciscdemux_src_link(GstPad *pad, GstPad *peer)
#endif
{
   Gstciscdemux *demux = NULL;
   GstElement *pElement;
   GstObject *peerParent;
#if GST_CHECK_VERSION(1,0,0)
   demux = GST_CISCDEMUX (parent);
#else
   demux = GST_CISCDEMUX (gst_pad_get_parent(pad));
#endif
   if(NULL == demux)
   {
      GST_ERROR("Gstciscdemux ptr is NULL\n");
      return GST_PAD_LINK_REFUSED;
   }

   gchar *padName = gst_pad_get_name(pad);
   gchar *peerName = gst_pad_get_name(peer);

   pElement = (GstElement*) gst_pad_get_parent_element (pad);
   GST_WARNING("---->[%s]Src Link with peer %s (Element %s)  %s (Pad %p %p)\n",
      __FUNCTION__, padName, GST_ELEMENT_NAME(pElement), peerName, pad, peer);

   if (pElement)
   {
      gst_object_unref (pElement);
   }

   pElement = (GstElement*) gst_pad_get_parent_element (peer);
   if (pElement)
   {
      GST_WARNING("Peer Element: %s\n", GST_ELEMENT_NAME(pElement));
      gst_object_unref (pElement);
   }

   g_free(padName);
   g_free(peerName);

   if (GST_PAD_LINKFUNC (peer)) {
#if GST_CHECK_VERSION(1,0,0)
      peerParent = gst_pad_get_parent (peer);
      GST_PAD_LINKFUNC (peer) (peer, peerParent, pad);
#else
      GST_PAD_LINKFUNC (peer) (peer, pad);
#endif
      gst_object_unref (peerParent);
   }

   demux->downstream_peer_pad = peer;

#if !GST_CHECK_VERSION(1,0,0)
   gst_object_unref(demux);
#endif

   return GST_PAD_LINK_OK;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean gst_cscohlsdemuxer_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
#else
static gboolean gst_cscohlsdemuxer_src_query (GstPad * pad, GstQuery * query)
#endif
{
   Gstciscdemux *demux;
   gboolean ret = FALSE;

   if (query == NULL)
      return FALSE;

#if GST_CHECK_VERSION(1,0,0)
   demux = GST_CISCDEMUX (parent);
#else
   demux = GST_CISCDEMUX (gst_pad_get_parent(pad));
#endif
   if(NULL == demux)
   {
      GST_ERROR("Gstciscdemux ptr is NULL\n");
      return FALSE;
   }
   switch (GST_QUERY_TYPE(query))
   {
      case GST_QUERY_DURATION:
      {
         GstClockTime duration;
         GstFormat fmt;

         gst_query_parse_duration (query, &fmt, NULL);
         if (fmt == GST_FORMAT_TIME) {
            duration = gst_cisco_hls_get_duration (demux);
            if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
               gst_query_set_duration (query, GST_FORMAT_TIME, duration);
               ret = TRUE;
            }
         }
         break;
      }

      case GST_QUERY_URI:
         if (demux->uri)
         {
            gst_query_set_uri (query, demux->uri);
            ret = TRUE;
         }
         break;

      case GST_QUERY_SEEKING:
      {
         ret = FALSE;
         break;
      }

      case GST_QUERY_CUSTOM:
      {
         ret = FALSE;
         GST_DEBUG("Custom query\n");
         GstStructure *pStruct = (GstStructure *)gst_query_get_structure(query);
         if(gst_structure_has_name(pStruct, "getTrickSpeeds"))
         {
            GST_DEBUG("getTrickSpeeds query\n");

            srcPluginGetData_t     getData = {};
            int                    bTrickSupported = 0;
            srcPluginContentType_t contentType;
            char                   speeds[128] = "";
            tSession               *pSession = NULL;
            srcPluginErr_t         errTable = {};
            srcStatus_t            stat = SRC_SUCCESS;

            pSession = demux->pCscoHlsSession;
            if ( pSession == NULL )
            {
               GST_ERROR("libhls session is NULL!\n");
               ret = FALSE;
               break;
            }

            getData.getCode = SRC_PLUGIN_GET_TRICK_SUPPORTED;
            getData.pData = &bTrickSupported;

            stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
            if (stat != SRC_SUCCESS)
            {
               GST_ERROR("There was an error obtaining trick supported boolean: %s\n",
                         errTable.errMsg);
               ret = FALSE;
               break;
            }

            getData.getCode = SRC_PLUGIN_GET_CONTENT_TYPE;
            getData.pData = &contentType;

            stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
            if (stat != SRC_SUCCESS)
            {
               GST_ERROR("There was an error obtaining content type: %s\n",
                         errTable.errMsg);
               ret = FALSE;
               break;
            }

            if(1 == bTrickSupported)
            {
              /* Hardcoding the speeds because libhls sends the I-Frames to the decoder
               * based on speed (duration/speed - i-frame display time)
               */
               strncpy(speeds, "-8,-4,-2,", sizeof(speeds));
            }

            if(SRC_PLUGIN_CONTENT_TYPE_VOD == contentType)
            {
               strncat(speeds, "0,", sizeof(speeds));
            }

            strncat(speeds, "1", sizeof(speeds));

            if(1 == bTrickSupported)
            {
               strncat(speeds, ",2,4,8", sizeof(speeds));

            }
            speeds[sizeof(speeds) - 1] = '\0';

            GST_DEBUG("%s %s(%d): Trick Speeds str: %s\n", __FILE__, __FUNCTION__, __LINE__, speeds);

            gst_structure_set(pStruct,
                             "trickSpeedsStr", G_TYPE_STRING, speeds,
                             NULL);
              ret = TRUE;
         }
         break;
      }
      default:
         /* Don't forward queries upstream because of the special nature of this
         * "demuxer", which relies on the upstream element only to be fed with the
         * first playlist */
         break;
   }

#if !GST_CHECK_VERSION(1,0,0)
   gst_object_unref(demux);
#endif
   return ret;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean gst_cscohlsdemuxer_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
#else
static gboolean gst_cscohlsdemuxer_src_event (GstPad * pad, GstEvent * event)
#endif
{
   Gstciscdemux *demux;
   gboolean ret = FALSE;


#if GST_CHECK_VERSION(1,0,0)
   demux = GST_CISCDEMUX (parent);
#else
   demux = GST_CISCDEMUX (gst_pad_get_parent(pad));
#endif

   if(NULL == demux)
   {
      GST_ERROR("Gstciscdemux ptr is NULL\n");
      return FALSE;
   }

   switch (GST_EVENT_TYPE(event))
   {
      case GST_EVENT_SEEK:
      {
         GST_WARNING("Got seek event!");
         ret = gst_cisco_hls_seek (demux, event);
         gst_event_unref(event);
#if !GST_CHECK_VERSION(1,0,0)
         gst_object_unref(demux);
#endif
         return ret;
      }
      default: 
#if GST_CHECK_VERSION(1,0,0)
         ret = gst_pad_event_default (pad, parent, event);
#else
         ret = gst_pad_event_default (pad, event);
#endif
         break;
   }

#if !GST_CHECK_VERSION(1,0,0)
   gst_object_unref(demux);
#endif

   return ret;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean gst_cscohlsdemuxer_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
#else
static gboolean gst_cscohlsdemuxer_sink_event (GstPad * pad, GstEvent * event)
#endif
{
   Gstciscdemux *demux;
   gboolean ret;

#if GST_CHECK_VERSION(1,0,0)
   demux = GST_CISCDEMUX (parent);
#else
   demux = GST_CISCDEMUX (gst_pad_get_parent(pad));
#endif

   if(NULL == demux)
   {
      GST_ERROR("Gstciscdemux ptr is NULL\n");
      return FALSE;
   }

   switch ( GST_EVENT_TYPE(event) )
   {
      case GST_EVENT_EOS:
      {
         cisco_hls_start(demux);
         gst_event_unref(event);
         break;
      }

#if GST_CHECK_VERSION(1,0,0)
      case GST_EVENT_SEGMENT:
#else
      case GST_EVENT_NEWSEGMENT:
#endif
         gst_event_unref(event);
         ret = TRUE;
         break;

      default:
#if GST_CHECK_VERSION(1,0,0)
         ret = gst_pad_event_default (pad, parent, event);
#else
         ret = gst_pad_event_default (pad, event);
#endif
         break;
   }

#if !GST_CHECK_VERSION(1,0,0)
   gst_object_unref(demux);
#endif

   return ret;
}

static GstStateChangeReturn gst_cscohlsdemuxer_change_state (GstElement * element, GstStateChange transition)
{
   GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
   Gstciscdemux *demux = GST_CISCDEMUX (element);
   srcPluginSetData_t setData = {};
   float speed = 0.0;
   srcStatus_t stat = SRC_SUCCESS;
   srcPluginErr_t errTable = {};
   tSession *pSession = NULL;

   if(NULL == demux)
   {
      GST_ERROR("ciscdemux ptr is NULL\n");
      return GST_STATE_CHANGE_FAILURE;
   }

   GST_DEBUG("HLS demux state change From %d To %d\n", (transition >> 3),(transition & 0x07));

   pSession = demux->pCscoHlsSession;

   switch (transition)
   {
      case GST_STATE_CHANGE_NULL_TO_READY:
         cisco_hls_initialize (demux);
         break;
      case GST_STATE_CHANGE_READY_TO_PAUSED:
      {
         gboolean ret;
         gchar *uri;
         GstQuery *query;

         query = gst_query_new_uri();
         ret = gst_pad_peer_query (demux->sinkpad, query);
         if (ret)
         {
            // http://foobar.com/index.m3u8?LicenseID=abcde456785345a&another
            gst_query_parse_uri(query, &uri);

            //
            // Parse for a LicenseID to pass down stream.
            //
            do
            {
               gchar *str = NULL;
               gchar **array = NULL;
               gchar delimiter ='&';

               str =  g_strstr_len (uri, -1, "LicenseID=");
               if (str == NULL) break;
               // copy string until you find '&'
               array = g_strsplit (str, &delimiter, 1024);
               // the first array element string should hold what we need.
               GST_WARNING_OBJECT(demux,"Setting LicenseID to: %s\n",array[0]);
               if (demux->LicenseID != NULL)
               {
                  g_free(demux->LicenseID);
               }
               demux->LicenseID = strdup (array[0]);
               g_strfreev(array);

            }while(0);

            cisco_hls_open(demux, uri);

            //ok free the uri string
            g_free(uri);
         }
         else
         {
            GST_ERROR("Could not query the HLS URI\n");
            return GST_STATE_CHANGE_FAILURE;
         }
         break;
      }
      case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
         cisco_hls_start(demux);
         break;
      default:
         break;
   }

   ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

   switch (transition)
   {
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
         cisco_hls_pause(demux);
         break;
      case GST_STATE_CHANGE_PAUSED_TO_READY:
         cisco_hls_close(demux);
         break;
      case GST_STATE_CHANGE_READY_TO_NULL:
         cisco_hls_finalize(demux);
         break;
      default:
         break;
   }

   return ret;
}

/* This is the begining of the integration code of the 
* cisco hls plugin
*/


const char *strHLSEvent [] =
{
   "SRC_PLUGIN_FORCED_RESUME    ",
   "SRC_PLUGIN_BUFFERING       ",
   "SRC_PLUGIN_SWITCHED_BITRATE",
   "SRC_PLUGIN_DRM_LICENSE     ",
   "SRC_PLUGIN_BOF             ",
   "SRC_PLUGIN_BOS             ",
   "SRC_PLUGIN_EOF             ",
   "SRC_PLUGIN_EOS            " 
};

void hlsPlayer_pluginEvtCallback(void* pHandle, srcPluginEvt_t* pEvt)
{   

   Gstciscdemux *demux = (Gstciscdemux *)pHandle;
   int ii = 0;

   GST_LOG(" Entered: %s : got event [%15s] callback for pHandle: %p : %d\n",
      __FUNCTION__, strHLSEvent[pEvt->eventCode],pHandle, pEvt->eventCode);

   if (demux == NULL)
      return;

   switch( pEvt->eventCode )
   {
      case SRC_PLUGIN_EOS:
      case SRC_PLUGIN_EOF:
      case SRC_PLUGIN_BOF:
      case SRC_PLUGIN_BOS:
         GST_INFO("Received boundary event(BOS/BOF/EOF/EOS)");
         gst_ciscdemux_send_eos(demux->srcpad);
         
         for(ii = 0; ii < demux->numSrcPadsActive - 1; ii++)
         {
            gst_ciscdemux_send_eos(demux->srcpad_discrete[ii]);
         }
         break;
#if 0
      case SRC_PLUGIN_BOF:
         gst_element_post_message(GST_ELEMENT_CAST(demux),
                                  gst_message_new_element(GST_OBJECT_CAST(demux),
                                  gst_structure_new("extended_notification",
                                  "notification", G_TYPE_STRING, "BOF",
                                  NULL)));
         break;
#endif
   }
   return;
}

void hlsPlayer_pluginErrCallback(void* pHandle, srcPluginErr_t* pErr)
{
   GST_ERROR(" Entered: %s : got event ERROR callback for pHandle: %p : %d\n",
      __FUNCTION__,pHandle, pErr->errCode);
   return ;
}

srcStatus_t hlsPlayer_registerCB(void* pHandle, playerEvtCallback_t evtCb)
{
   srcStatus_t status = SRC_SUCCESS;
   Gstciscdemux *demux = (Gstciscdemux *)pHandle;

   do
   {
      demux->playerEvtCb = evtCb;
   }while (0);

   return status;
}

srcStatus_t hlsPlayer_getBuffer(void* pHandle, char** buffer, int* size, void **ppPrivate)
{
   srcStatus_t status = SRC_SUCCESS;
   tMemoryStruct *pmem = NULL;
   Gstciscdemux *demux = (Gstciscdemux*) pHandle;

   do
   {
      /* Consider doing this. HLS pushes buffers even when paused
       * We can block it by not returning a buffer here
      if (demux->speed == 0.0)
      {
         *ppPrivate = NULL;
         *buffer = NULL;
         *size = 0;
         return SRC_SUCCESS;
      }
      */

      // we need to rate limit a bit
      pmem = (tMemoryStruct*) g_malloc(sizeof(tMemoryStruct));
      if (G_UNLIKELY (pmem == NULL))
      {
         GST_ERROR("Error getting pmem structure\n");
         return SRC_ERROR;
      }

      //TODO Make this buffer the size of the pipeline buffer
#if GST_CHECK_VERSION(1,0,0)
      pmem->buf = gst_buffer_new_allocate (NULL, 4096, NULL);
#else
      pmem->buf = gst_buffer_try_new_and_alloc(4096);
#endif
      if (G_UNLIKELY (pmem->buf == NULL))
      {
         g_free (pmem);
         GST_ERROR("Error getting GstBuffer\n");
         return SRC_ERROR;
      }

#if GST_CHECK_VERSION(1,0,0)
      if (gst_buffer_map (pmem->buf, &pmem->info, GST_MAP_WRITE) == FALSE)
      {
         gst_buffer_unref(pmem->buf);
         g_free (pmem);
         GST_ERROR("Error mapping GstBuffer\n");
         return SRC_ERROR;
      }

      pmem->memory = pmem->info.data;
      pmem->size = pmem->info.size;
#else
      pmem->memory = GST_BUFFER_DATA(pmem->buf);
      pmem->size = 4096;
#endif
      pmem->bInUse = TRUE;

      *ppPrivate = (void*)pmem;
      *size = pmem->size;
      *buffer = pmem->memory;

   }while (0);

   return status;
}

srcStatus_t hlsPlayer_sendBuffer(void* pHandle, char* buffer, int size, srcBufferMetadata_t* metadata, void *pPrivate)
{
   srcStatus_t status = SRC_ERROR;
   tMemoryStruct *pmem = NULL;
   GstBuffer *buf = NULL;
   GstEvent*event= NULL;
   GstPad *srcpad = NULL;
   gchar *pad_name = NULL;

   // send it downstream first we need to see if we need to set the capabilities
   Gstciscdemux *demux = (Gstciscdemux*) pHandle;

   do
   {
      if(NULL == demux)
      {
         GST_ERROR("%s() demux is NULL\n", __FUNCTION__);
         break;
      }

      if(NULL == metadata)
      {
         GST_ERROR("%s() metadata is NULL\n", __FUNCTION__);
         break;
      }

      if((NULL == demux->srcpad_discrete) && (metadata->totalNumStreams > 1))
      {
         demux->srcpad_discrete = (GstPad **)g_malloc((metadata->totalNumStreams - 1) * sizeof(GstPad *));
         memset(demux->srcpad_discrete, 0, sizeof(GstPad *) * (metadata->totalNumStreams - 1));
      }

      if(NULL == demux->inputStreamCap)
      {
         demux->inputStreamCap = (GstCaps **)g_malloc(metadata->totalNumStreams * sizeof(GstCaps *));
         memset(demux->inputStreamCap, 0, sizeof(GstCaps *) * metadata->totalNumStreams);
      }

      if (pPrivate == NULL)
      {
         GST_ERROR("Warning %s pPrivate is NULL\n", __FUNCTION__);
         break;
      }
      pmem = (tMemoryStruct*)pPrivate;

#if GST_CHECK_VERSION(1,0,0)
      gst_buffer_unmap (pmem->buf, &pmem->info);
#endif

      pmem->size = size; // this is the ACTUAL data amount written to the buffer, the size of the actual
      // buffer is still the original 4096 bytes

      buf = pmem->buf;
      if (metadata->pts > INVALID_PTS)
      {
         demux->bufferPts = metadata->pts;
      }
#if GST_CHECK_VERSION(1,0,0)
      gst_buffer_set_size (buf, size);
      GST_BUFFER_PTS (buf) = (demux->bufferPts * GST_MSECOND) / 90;
#else
      GST_BUFFER_SIZE (buf) = size;
      GST_BUFFER_TIMESTAMP (buf) = (demux->bufferPts * GST_MSECOND) / 90;
#endif

      // we don't need the pPrivate anymore
      g_free (pmem);
      
      if((metadata->streamNum > SRC_STREAM_NUM_MAIN) && (NULL == demux->srcpad_discrete[metadata->streamNum - 1]))
      {
         if(TRUE != gst_ciscdemux_get_caps(metadata, buf, &demux->inputStreamCap[metadata->streamNum]))
         {
            break;
         }
         
         GST_INFO_OBJECT(demux, "Discrete stream %d Capabilities: %" GST_PTR_FORMAT, 
                         metadata->streamNum - 1, demux->inputStreamCap[metadata->streamNum]);

         pad_name = g_strdup_printf ("src_discrete_%02d", metadata->streamNum - 1);     
         GST_INFO_OBJECT(demux, "Adding discrete src pad (name:%s)...\n", pad_name);
         demux->srcpad_discrete[metadata->streamNum - 1] = gst_pad_new_from_static_template (&src_discrete_factory, pad_name);     
         g_free (pad_name);

#if GST_CHECK_VERSION(1,0,0)
         gst_pad_use_fixed_caps(demux->srcpad_discrete[metadata->streamNum -1]);
#endif
         if(FALSE == gst_pad_set_active (demux->srcpad_discrete[metadata->streamNum - 1], TRUE))
         {
            GST_WARNING_OBJECT(demux,"I was not able to set the discrete(%d) src pad to active\n!", 
                               metadata->streamNum - 1); 
         }
         
         if(FALSE == gst_pad_set_caps (demux->srcpad_discrete[metadata->streamNum - 1], demux->inputStreamCap[metadata->streamNum]))
         {
            GST_WARNING_OBJECT(demux,"I was not able to set the discrete(%d) src pad to active\n!",
                               metadata->streamNum - 1); 
         }
         
         gst_element_add_pad (GST_ELEMENT_CAST (demux), demux->srcpad_discrete[metadata->streamNum - 1]);

         demux->numSrcPadsActive++;
         if(metadata->totalNumStreams == demux->numSrcPadsActive)
         {
            gst_element_no_more_pads(GST_ELEMENT_CAST(demux));
         }
      }
      // check to see if we have setup the source pad yet to send out it's capabilities
      else if((SRC_STREAM_NUM_MAIN == metadata->streamNum) && (demux->capsSet == 0))
      {
         if(TRUE != gst_ciscdemux_get_caps(metadata, buf, &demux->inputStreamCap[metadata->streamNum]))
         {
            break;
         }
         
         GST_INFO_OBJECT(demux, "Main stream Capabilities: %" GST_PTR_FORMAT, 
                         demux->inputStreamCap[metadata->streamNum]);

         gst_pad_set_event_function(demux->srcpad, GST_DEBUG_FUNCPTR(gst_cscohlsdemuxer_src_event));

         gst_pad_set_query_function (demux->srcpad, GST_DEBUG_FUNCPTR (gst_cscohlsdemuxer_src_query));

         gst_pad_set_element_private (demux->srcpad, demux);

#if GST_CHECK_VERSION(1,0,0)
         gst_pad_use_fixed_caps(demux->srcpad);
#endif

         if (FALSE == gst_pad_set_active (demux->srcpad, TRUE))
         {
            GST_WARNING_OBJECT(demux,"I was not able to set the main src pad to active\n!"); 
         }

         if (FALSE == gst_pad_set_caps (demux->srcpad, demux->inputStreamCap[metadata->streamNum]))
         {
            GST_WARNING_OBJECT(demux, "I was not able to set caps on the main src pad for some reason\n");
         }

         demux->capsSet = 1;
         
         demux->numSrcPadsActive++;
         if(metadata->totalNumStreams == demux->numSrcPadsActive)
         {
            gst_element_no_more_pads(GST_ELEMENT_CAST(demux));
         }

         if (pthread_create(&demux->getPTSThread, NULL, getCurrentPTSNotify, demux) == 0)
         {
            demux->bGetPTSThreadRunning = TRUE;
         }

         if(TRUE == demux->bDisableMainStreamAudio)
         {
            if(TRUE != gst_ciscdemux_disable_main_stream_audio(demux))
            {
               GST_ERROR("Failed to disable main stream audio\n");
               break;
            }
         }
      }// end of if capsSet

      if(SRC_STREAM_NUM_MAIN == metadata->streamNum)
      {
         srcpad = demux->srcpad;
      }
      else
      {
         srcpad = demux->srcpad_discrete[metadata->streamNum - 1];
      }

      // Now that the pad is setup and the capabilities set on the pad we can send a decryption event
      // if necessary or we can just send the buffer down the pipeline this is outside the above block 
      // because you can get a new key tag at any time
      if (1 == metadata->bFirstBufferInSegment)
      {
         if (metadata->encType ==SRC_ENC_AES128_CBC)
         {
            // now we can send the event downstream. :-)
            event = gst_ciscdemux_create_decryption_event(metadata,demux); 
            if (event == NULL) { GST_ERROR("Error no event to send\n");}

            GST_INFO_OBJECT(demux, "Sending the encryption key information downstream\n");
            if (gst_pad_push_event(srcpad, event)== FALSE)
            {
               GST_WARNING(" Error sending the encyption key down stream\n");
            }
         }// end of if metadata->SRC_ENC_AES128CBC
      } // end of if bFirstBufferInSegment

#if !GST_CHECK_VERSION(1,0,0)
      gst_buffer_set_caps(buf, demux->inputStreamCap[metadata->streamNum]);
#endif

      gst_pad_push(srcpad, buf);

      status = SRC_SUCCESS;

   }while(0);

   return status;
}

srcStatus_t hlsPlayer_set(void *pHandle, srcPlayerSetData_t *pSetData)
{
   srcStatus_t status = SRC_SUCCESS;

   Gstciscdemux *demux = (Gstciscdemux *)pHandle;   

   if (demux == NULL)
      return SRC_ERROR;
   
   switch(pSetData->setCode)
   {
      case SRC_PLAYER_SET_BUFFER_FLUSH:
         {
            GST_INFO_OBJECT(demux, "Received SRC_PLAYER_SET_BUFFER_FLUSH");
            if(TRUE != gst_ciscdemux_send_flush_to_all_srcpads(demux))
            {
               status = SRC_ERROR;
            }
         }
         break;

      case SRC_PLAYER_SET_DISABLE_MAIN_STREAM_AUDIO:
         {
            demux->bDisableMainStreamAudio = TRUE;
         }
         break;

      case SRC_PLAYER_SET_MODE:
         {
            GST_INFO_OBJECT(demux, "Received SRC_PLAYER_SET_MODE");
            if(NULL == pSetData)
            {
               GST_WARNING_OBJECT(demux, "SRC_PLAYER_SET_MODE - pData is NULL");
               break;
            }

            if(SRC_PLAYER_MODE_LOW_DELAY == *((srcPlayerMode_t *)pSetData->pData))
            {
               GST_INFO_OBJECT(demux, "Received SRC_PLAYER_MODE_LOW_DELAY");
               gst_ciscdemux_send_low_delay_videomask_event(demux, TRUE);
            }
            else if(SRC_PLAYER_MODE_NORMAL == *((srcPlayerMode_t *)pSetData->pData))
            {
               GST_INFO_OBJECT(demux, "Received SRC_PLAYER_MODE_NORMAL");
               gst_ciscdemux_send_low_delay_videomask_event(demux, FALSE);
            }
         }
         break;

      default:
         break;
   }

   return status;
}

srcStatus_t hlsPlayer_get(void *pHandle, srcPlayerGetData_t *pGetData)
{
   srcStatus_t status = SRC_SUCCESS;

   do
   {
   }while (0);

   return status;
}

srcPlayerFunc_t gPlayerFunc =
{
   .registerCB = hlsPlayer_registerCB,
   .getBuffer  = hlsPlayer_getBuffer,
   .sendBuffer = hlsPlayer_sendBuffer,
   .set =        hlsPlayer_set,
   .get =        hlsPlayer_get,
};



static gboolean cisco_hls_initialize (Gstciscdemux *demux)
{
   srcStatus_t stat =SRC_SUCCESS;
   srcPluginErr_t errTable;
   gboolean   bError = FALSE;

   do
   {
      memset(errTable.errMsg, 0, SRC_ERR_MSG_LEN);
      //from hlsPlugin.c 
      // the demux->HLS_pluginTable will be empty
      // we pass in a filled in version of the gPlayerFunc table
      // btw it is rediculous that I have to do this with the playerfunctions.  
      // The source code directly calls these functions.
      stat = srcPluginLoad( &demux->HLS_pluginTable, /* empty structure*/
         &gPlayerFunc , /* filled in functions of our player in this case GST wrapper*/
         &errTable ); /* No debug string support for now */
      if (stat != SRC_SUCCESS)
      {
         GST_ERROR(" Hummm there was an error loading the HLS plugin:%s\n", errTable.errMsg);
         bError =TRUE;
         break;
      }

      if (demux->HLS_pluginTable.initialize == NULL )
      {
         GST_ERROR("Plugin function pointer is null\n");
         break;
      }


      stat = demux->HLS_pluginTable.initialize(&errTable);
      if (stat != SRC_SUCCESS)
      {
         GST_ERROR(" Hummm there was an error initialzing the HLS plugin:%s\n", errTable.errMsg);
         bError =TRUE;
         break;
      }
      else
      {
         GST_LOG("Done loading the HLS src plugin\n"); 
      }

      // ok now we need to register the event callback
      //
      stat = demux->HLS_pluginTable.registerCB(hlsPlayer_pluginEvtCallback, hlsPlayer_pluginErrCallback, &errTable);
      if (stat != SRC_SUCCESS)
      {
         GST_ERROR(" Hummm there was an error registering the HLS plugin:%s\n", errTable.errMsg);
         bError =TRUE;
         break;
      }
      else
      {
         GST_LOG("Done loading the HLS callbacks\n"); 
      }

   } while(0);

   return bError;
}

static gboolean cisco_hls_open (Gstciscdemux *demux, char *pPlaylistUri)
{
   srcStatus_t stat = SRC_SUCCESS;
   srcPluginErr_t errTable;
   tSession *pSession = NULL;
   gboolean bError = FALSE;
   srcPluginSetData_t setData = {};
   int                minBitrate = 0;

   do 
   {
      demux->pCscoHlsSession = pSession = (tSession *) g_malloc(sizeof(tSession));
      pSession->pHandle_Player = demux;
      stat = demux->HLS_pluginTable.open(&pSession->pSessionID, pSession->pHandle_Player, &errTable);
      if (stat != SRC_SUCCESS)
      {
         GST_ERROR(" Hummm there was an error opening the HLS plugin:%s\n", errTable.errMsg);
         bError = TRUE;
         break;
      }
      else
      {
         GST_LOG("Done opening the HLS callbacks\n"); 
      }
         
      setData.setCode = SRC_PLUGIN_SET_MIN_BITRATE;
      minBitrate = 100000;
      setData.pData = &minBitrate;
      stat = demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable );
      if(stat)
      {
         GST_ERROR( "%s: Error %d while setting minBitrate to %d: %s", 
                    __FUNCTION__, errTable.errCode, minBitrate, errTable.errMsg);
      }

      // First save the uri to our local object
      if (demux->uri)
      {
         g_free(demux->uri);
      }

      if (gst_uri_is_valid(pPlaylistUri))
      {
         demux->uri = strndup(pPlaylistUri, 512);
      }
      else
      {
         GST_WARNING_OBJECT(demux, "Passed in URI is NOT valid");
         bError = TRUE;
         break;
      }

      //Now set the uri with the cisco hls plugin and let's kick it off.
      setData.setCode = SRC_PLUGIN_SET_DATA_SOURCE;
      setData.pData = strndup(demux->uri, 512 );

      GST_LOG("Setting location to : %s \n",(char*) setData.pData);

      /* now set the location of the m3u8 url to the HLS plugin */
      stat = demux->HLS_pluginTable.set(pSession->pSessionID, &setData, &errTable);

      g_free(setData.pData);

      if (stat != SRC_SUCCESS)
      {
         GST_ERROR(" Hummm there was an error setting the url to the HLS plugin:%s\n", errTable.errMsg);
         bError = TRUE;
         break;
      }

      /* prepare */
      stat = demux->HLS_pluginTable.prepare(pSession->pSessionID, &errTable );
      if(stat)
      {
         GST_ERROR( "%s: Error %d while preparing playlist: %s", __FUNCTION__, errTable.errCode, errTable.errMsg);
         bError = TRUE;
      }
   
   }while(0);

   return bError;
}

static gboolean cisco_hls_start(Gstciscdemux *demux)
{
   srcStatus_t stat = SRC_SUCCESS;
   srcPluginErr_t errTable;
   srcPluginSetData_t setData;
   tSession *pSession = demux->pCscoHlsSession;

   if (demux->speed != 1.0)
   {
      GST_DEBUG( "%s: Setting HLS to playing...\n", __FUNCTION__);

      setData.setCode = SRC_PLUGIN_SET_SPEED;
      setData.pData = g_malloc(sizeof(float));
      *(float *)(setData.pData) = 1.0;
      /* setSpeed (play) */
      stat = demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable );
      if(stat)
      {
         GST_ERROR( "%s: Error %d while setting speed to 1: %s", __FUNCTION__, errTable.errCode, errTable.errMsg);
         g_free(setData.pData);
         return FALSE;
      }
      g_free(setData.pData);
      setData.pData = NULL;

      demux->speed = 1.0;
   }

   return TRUE;
}

static gboolean cisco_hls_pause(Gstciscdemux *demux)
{
   srcStatus_t stat = SRC_SUCCESS;
   srcPluginErr_t errTable;
   srcPluginSetData_t setData;
   tSession *pSession = demux->pCscoHlsSession;

   if (demux->speed != 0.0)
   {
      GST_DEBUG( "%s: Setting HLS to playing...\n", __FUNCTION__);

      setData.setCode = SRC_PLUGIN_SET_SPEED;
      setData.pData = g_malloc(sizeof(float));
      *(float *)(setData.pData) = 0.0;
      /* setSpeed (play) */
      stat = demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable );
      if(stat)
      {
         GST_ERROR( "%s: Error %d while setting speed to 0: %s", __FUNCTION__, errTable.errCode, errTable.errMsg);
         g_free(setData.pData);
         return FALSE;
      }
      g_free(setData.pData);
      setData.pData = NULL;

      demux->speed = 0.0;
   }

   return TRUE;
}

static gboolean cisco_hls_close(Gstciscdemux *demux)
{
   srcPluginErr_t errTable;
   srcStatus_t stat = SRC_SUCCESS;

   pthread_mutex_lock(&demux->PTSMutex);
   demux->bKillPTSThread = TRUE;
   GST_DEBUG("Timestamp before killing pts thread : %llu\n", (unsigned long long)time(NULL));
   /* Wake up the PTS thread if it is sleeping */
   pthread_cond_signal(&demux->PTSThreadCond);
   pthread_mutex_unlock(&demux->PTSMutex);

   if (TRUE == demux->bGetPTSThreadRunning)
   {
      pthread_join(demux->getPTSThread, NULL);
      demux->bGetPTSThreadRunning = FALSE;
   }

   GST_DEBUG("Timestamp after killing pts thread : %llu\n", (unsigned long long)time(NULL));

   stat = demux->HLS_pluginTable.close(demux->pCscoHlsSession->pSessionID, &errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error closing the HLS plugin:%s\n", errTable.errMsg);
   }
   else
   {
      GST_LOG("Done closing the HLS plugin session\n"); 
   }
   GST_DEBUG("Timestamp when hls close returns : %llu\n", (unsigned long long)time(NULL));

   g_free(demux->pCscoHlsSession);
   if(demux->LicenseID){g_free(demux->LicenseID);}
   demux->pCscoHlsSession = NULL;

   return TRUE;
}

static gboolean cisco_hls_finalize(Gstciscdemux *demux)
{
   srcPluginErr_t errTable;
   srcStatus_t stat = SRC_SUCCESS;
   int ii = 0;

   stat = demux->HLS_pluginTable.finalize(&errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error finalizing the HLS plugin:%s\n", errTable.errMsg);
   }
   else
   {
      GST_LOG("Done finalizing the HLS src plugin\n"); 
   }

   if(0 != pthread_mutex_destroy(&demux->PTSMutex))
   {
      GST_WARNING("Failed to destroy PTSMutex\n");
   }

   if(0 != pthread_cond_destroy(&demux->PTSThreadCond))
   {
      GST_WARNING("Failed to destroy PTSThreadCond\n");
   }

   if(NULL != demux->inputStreamCap)
   {
      for(ii = 0; ii < demux->numSrcPadsActive; ii++)
      {
         gst_caps_unref(demux->inputStreamCap[ii]);
         demux->inputStreamCap[ii] = NULL;
      }
      g_free(demux->inputStreamCap);
      demux->inputStreamCap = NULL;
   }
   
   if(NULL != demux->srcpad_discrete)
   {
      g_free(demux->srcpad_discrete);
      demux->srcpad_discrete = NULL;
   }

   return TRUE;
}

static GstClockTime gst_cisco_hls_get_duration (Gstciscdemux *demux)
{
   srcPluginGetData_t getData;
   srcPluginErr_t errTable;
   GstClockTime  t;
   srcStatus_t stat = SRC_SUCCESS;
   tSession *pSession = demux->pCscoHlsSession;


   getData.getCode = SRC_PLUGIN_GET_DURATION;
   getData.pData = g_malloc(sizeof(float));

   /* now set the location of the m3u8 url to the HLS plugin */
   stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error obtaining the duration: %s\n", errTable.errMsg);
   }

   t = *((float *)getData.pData);
   t = t * GST_MSECOND; // turn ms into gstreamer time base
   g_free(getData.pData);

   GST_LOG("[cischlsdemux] - duration = %llu\n", (unsigned long long)t);
   // returned value is in nanoseconds.
   return t;
}

static gboolean gst_cisco_hls_seek (Gstciscdemux *demux, GstEvent *event)
{
   gfloat position;
   gfloat speed = 0.0;
   srcPluginGetData_t getData = {};
   srcPluginSetData_t setData;
   srcPluginErr_t errTable;
   GstClockTime timestamp;
   srcStatus_t stat = SRC_SUCCESS;
   tSession *pSession;
   GstFormat format;
   gdouble rate;
   GstSeekFlags flags;
   GstSeekType curType, stopType;
   gint64 cur, stop;

   if ( demux == NULL )
   {      
      return FALSE;
   }

   pSession = demux->pCscoHlsSession;
   if ( pSession == NULL )
   {      
      GST_ERROR("HLS session not opened!\n");
      return FALSE;
   }

   timestamp = GST_EVENT_TIMESTAMP(event);
   /*
   if (timestamp == GST_CLOCK_TIME_NONE)
   {
      printf("[ciscdemux] - invalid seek position!\n");
      return FALSE;
   }
   */

   /* Get the current speed from libhls */
   getData.getCode = SRC_PLUGIN_GET_SPEED;
   getData.pData = &speed;

   stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error obtaining the speed: %s\n", errTable.errMsg);
      return FALSE;
   }

   gst_event_parse_seek (event, &rate, &format, &flags, &curType, &cur, &stopType, &stop);

   GST_WARNING("[cischlsdemux] current speed: %f, rate: %f\n", speed, rate);
  
   if((speed == rate) && (GST_FORMAT_TIME == format))
   {
      //if we are paused currently, before doing a seek, send a flush downstream to
      //empty the demux buffers and unblock the libhls internal download loop
      //otherwise libhls may deadlock badly trying to kill the downloader
      //thread which may be blocked on demux buffers
      if (GST_STATE(demux) == GST_STATE_PAUSED)
      {
         if (NULL != demux->srcpad)
            gst_ciscdemux_flush(demux, demux->srcpad);
      }

      demux->isFlushOnSeek = TRUE;
      demux->seekpos = cur;

      setData.setCode = SRC_PLUGIN_SET_POSITION;
      position = (gfloat)(cur / GST_MSECOND);
      setData.pData = &position;
      GST_WARNING("[cischlsdemux] seeking to position %f, timestamp %"G_GINT64_FORMAT"...\n", position, cur);
      stat = demux->HLS_pluginTable.set(pSession->pSessionID, &setData, &errTable);
      if ( stat == SRC_ERROR )
      {
         GST_WARNING("Failed to set position on the source plugin: %s\n", errTable.errMsg);
         demux->isFlushOnSeek = FALSE;
         return FALSE;
      }
   }
   else
   {
      setData.setCode = SRC_PLUGIN_SET_SPEED;
      speed = rate;
      setData.pData = &speed; 
      stat = demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable );
      if(stat)
      {
         GST_ERROR("%s: Error %d while setting speed to %f: %s", __FUNCTION__, 
                   errTable.errCode, speed, errTable.errMsg);
         return FALSE;
      }
      GST_LOG("setSpeed returned\n");
   }

   demux->speed = speed;

   return TRUE;
}

static void * getCurrentPTSNotify(void *data)
{
   GstQuery        *query = NULL;
   GstStructure    *structure;
   const GValue    *val = NULL;
   gpointer        *ptr = NULL;
   gboolean        rc;
   unsigned int    pts_45khz = 0;
   long long       pts_90khz = 0;
   Gstciscdemux    *demux = (Gstciscdemux *)data;
   srcPlayerEvt_t  ptsEvent = {SRC_PLAYER_LAST_PTS, NULL};
   struct timespec ts = {};

   GST_LOG("%s() >>>>\n", __FUNCTION__);

   do
   {
      if(NULL == demux)
      {
         GST_WARNING("demux is NULL\n");
         break;
      }
      if(NULL == demux->pCscoHlsSession)
      {
         GST_WARNING("libhls session is NULL\n");
         break;
      }

      do
      {
         pthread_mutex_lock(&demux->PTSMutex);

         if(TRUE == demux->bKillPTSThread)
         {
            pthread_mutex_unlock(&demux->PTSMutex);
            break;
         }

         ts.tv_sec = time(NULL) + SRC_PTS_NOTIFY_INTERVAL;
         pthread_cond_timedwait(&demux->PTSThreadCond, &demux->PTSMutex, &ts);

         if(TRUE == demux->bKillPTSThread)
         {
            pthread_mutex_unlock(&demux->PTSMutex);
            break;
         }

         pthread_mutex_unlock(&demux->PTSMutex);

         if(NULL == demux->downstream_peer_pad)
         {
            /* Try again - srcpad is not linked */
            continue;
         }

         structure = gst_structure_new("get_current_pts", "current_pts", G_TYPE_UINT, 0, NULL);
#if GST_CHECK_VERSION(1,0,0)
         query = gst_query_new_custom(GST_QUERY_CUSTOM, structure);
#else
         query = gst_query_new_application(GST_QUERY_CUSTOM, structure);
#endif
         rc = gst_pad_query(demux->downstream_peer_pad, query);
         if (!rc)
         {
            GST_WARNING("could not get pts");
            gst_query_unref(query);
            continue;
         }

         structure = (GstStructure *)gst_query_get_structure(query);
         val = gst_structure_get_value(structure, "current_pts");
         if (val == NULL)
         {
            GST_WARNING("could not get pts");
            gst_query_unref(query);
            continue;
         }    

         ptr = g_value_get_pointer(val);
         if(NULL == ptr)
         {
            GST_WARNING("pts value is 0\n");
            gst_query_unref(query);
            continue;
         }

         memcpy((gchar *)&pts_45khz, (gchar *)&ptr, sizeof(pts_45khz)); 

         GST_LOG("Current 45khz based PTS %u\n", pts_45khz);
         pts_90khz = ((long long)pts_45khz) << 1;
         GST_LOG("Current 90khz based PTS %lld\n", pts_90khz);

         gst_query_unref(query);

         if(NULL != demux->playerEvtCb)
         {
            ptsEvent.pData = &pts_90khz;
            demux->playerEvtCb(demux->pCscoHlsSession->pSessionID, &ptsEvent);
         }
         else
         {
            GST_WARNING("playerEvtCb is NULL\n");
            break;
         }
      }while(1);

   }while(0);

   GST_LOG("%s() <<<<\n", __FUNCTION__);
   pthread_exit(NULL);
}

static gboolean gst_ciscdemux_get_caps(srcBufferMetadata_t *metadata,
                                       GstBuffer *buf,
                                       GstCaps **caps)
{
   gboolean ret = FALSE;

   do {
   if((NULL == metadata) || (NULL == buf) || (NULL == caps))
   {
      GST_ERROR("Invalid param\n");
      break;
   }

   // this is for basic HLS
   if (metadata->encType == SRC_ENC_AES128_CBC)
   {
#ifdef OPT_FORCE_VGDRM
      *caps = gst_caps_new_simple ("drm/x-VGDRM", NULL, NULL);
#elif OPT_FORCE_VERIMATRIX
      *caps = gst_caps_new_simple ("drm/x-VERIMATRIX", NULL, NULL);
#else
      *caps = gst_caps_new_simple ("drm/x-BASIC_HLS", NULL, NULL);
#endif
   }
   else if ( metadata->encType == SRC_ENC_NONE)
   {
      *caps = gst_type_find_helper_for_buffer(NULL, buf, NULL);

      if (NULL == *caps)
      {
         break;
      }
      
      GST_INFO("Stream %d capabilities: %" GST_PTR_FORMAT, metadata->streamNum, *caps);

      if (gst_caps_get_size(*caps) > 0)
      {
         GstStructure *structure = gst_caps_get_structure(*caps, 0);
         const gchar *mime = gst_structure_get_name(structure);
         if (mime != NULL && !g_ascii_strcasecmp(mime, "application/x-id3"))
         {
            gst_caps_unref (*caps);               
            *caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4, NULL);               
            if (*caps == NULL)
            {
               GST_WARNING("Could not obtain discrete audio caps for elementary audio stream!\n");
            }
         }
      }
   }
   else
   {
      *caps = NULL;
      GST_WARNING("I don't know this encryption type\n");
      break;
   }

   ret = TRUE;
   }while(0);

   return ret;
}

static gboolean gst_ciscdemux_flush(Gstciscdemux *demux, GstPad *srcpad)
{
   GstEvent *event;
   GstEvent *segmentEvent = NULL;
   gboolean ret = FALSE;

   do {

      if (srcpad == NULL)
      {
         GST_ERROR("source pad not linked!\n");
         break;
      }

      event = gst_event_new_flush_start ();
      if (event == NULL)
      {
         break;
      }

      GST_LOG("cisco demux sending flush start downstream...\n");
      gst_pad_push_event (srcpad, event);

#if GST_CHECK_VERSION(1,0,0)
      event = gst_event_new_flush_stop (FALSE);
#else
      event = gst_event_new_flush_stop ();
#endif
      if (event == NULL)
      {
         break;
      }

      GST_LOG("cisco demux sending flush stop downstream...\n");
      gst_pad_push_event (srcpad, event);

      if (demux->isFlushOnSeek == TRUE)
      {
         //Send a segment event downstream
#if GST_CHECK_VERSION(1,0,0)
         GstSegment segment;
         segment.rate = 1.0;
         segment.applied_rate = 1.0;
         segment.format = GST_FORMAT_TIME;
         segment.start = demux->seekpos;
         segment.stop = GST_CLOCK_TIME_NONE;
         segment.position = 0;

         segmentEvent = gst_event_new_segment (&segment);
#else
         segmentEvent = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, demux->seekpos, GST_CLOCK_TIME_NONE, 0);
#endif
         if (NULL != segmentEvent)
         {
            GST_WARNING("[cischlsdemux] sending segment event downstream for timestamp %"G_GINT64_FORMAT"...\n", demux->seekpos);
            if (gst_pad_push_event (srcpad, segmentEvent) != TRUE)
               GST_WARNING("[cischlsdemux] sending segment event downstream failed!");
         }

         demux->isFlushOnSeek = FALSE;
      }

      ret = TRUE;
   }while(0);
   
   return ret;
}

static gboolean gst_ciscdemux_disable_main_stream_audio(Gstciscdemux *demux)
{
   GstEvent     *event = NULL;
   GstStructure *structure = NULL;       
   gboolean     ret = FALSE;

   do 
   {
      structure = gst_structure_new("disable-audio-stream", NULL);
      if(NULL == structure)
      {
         GST_ERROR("Error creating disable-audio-stream structure\n");
         break;
      }

      event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
      if(NULL == event)
      {
         GST_ERROR("Error creating disable-audio-stream event\n");
         break;
      }

      GST_INFO("Sending disable-audio-stream event downstream\n");

      if (gst_pad_push_event(demux->srcpad, event)== FALSE)
      {
         GST_ERROR(" Error sending audio-stream event down stream\n");
         break;
      }

      ret = TRUE;
   }while(0);

   return ret;
}

static gboolean gst_ciscdemux_send_eos(GstPad *srcpad)
{
   gboolean ret = TRUE;
   GstEvent *event = NULL;

   if (srcpad != NULL)
   {
      event = gst_event_new_eos();
      if (gst_pad_push_event (srcpad, event) == FALSE)
      {
         GST_WARNING("Error sending the eos event down stream\n");
      }
   }

   return ret;
}

static gboolean gst_ciscdemux_send_low_delay_videomask_event(Gstciscdemux *demux, gboolean enable)
{
   gboolean     ret = FALSE;
   GstEvent     *event = NULL;
   GstStructure *structure = NULL;       
   gchar        video_mask_str[16] = "";
  
   do {
   if(TRUE == enable)
   {
      strncpy(video_mask_str, "i_only", sizeof(video_mask_str));
   }
   else
   {
      strncpy(video_mask_str, "all", sizeof(video_mask_str));
   }
   video_mask_str[sizeof(video_mask_str) - 1] = '\0';

   structure = gst_structure_new("low-delay", "enable", G_TYPE_BOOLEAN, enable, NULL);
   if(NULL == structure)
   {
      GST_ERROR("Error creating low-delay structure\n");
      break;
   }

   event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
   if(NULL == event)
   {
      GST_ERROR("Error creating low-delay event\n");
      break;
   }

   GST_DEBUG("Sending low-delay custom downstream event\n");

   ret = gst_ciscdemux_send_event_to_all_srcpads(demux, event);
   if(TRUE != ret)
   {
      GST_ERROR("Error sending low-delay event\n");
      break;
   }
   
   structure = gst_structure_new("video-mask", "video-mask-str", G_TYPE_STRING, video_mask_str, NULL);
   if(NULL == structure)
   {
      GST_ERROR("Error creating video-mask structure\n");
      break;
   }

   event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
   if(NULL == event)
   {
      GST_ERROR("Error creating video-mask event\n");
      break;
   }

   GST_DEBUG("Sending video-mask custom downstream event\n");

   ret = gst_ciscdemux_send_event_to_all_srcpads(demux, event);
   if(TRUE != ret)
   {
      GST_ERROR("Error sending video-mask event\n");
      break;
   }
   
   ret = TRUE;
   }while(0); 

   return ret;
}

static gboolean gst_ciscdemux_send_event_to_all_srcpads(Gstciscdemux *demux, GstEvent *event)
{
   gboolean ret = FALSE;
   int      ii = 0;

   do {
   ret = gst_pad_push_event(demux->srcpad, event);
   if(TRUE != ret)
   {
      GST_ERROR("Error sending low-delay event\n");
      break;
   }

   for(ii = 0; ii < demux->numSrcPadsActive - 1; ii++)
   {
      ret = gst_pad_push_event(demux->srcpad_discrete[ii], event);
      if(TRUE != ret)
      {
         GST_ERROR("Error sending low-delay event\n");
         break;
      }
   }

   }while(0);

   return ret;
}

static gboolean gst_ciscdemux_send_flush_to_all_srcpads(Gstciscdemux *demux)
{
   gboolean ret = FALSE;
   gint ii = 0;

   do {
   ret = gst_ciscdemux_flush(demux, demux->srcpad);
   if(TRUE != ret)
   {
      break;
   }
   
   for(ii = 0; ii < demux->numSrcPadsActive - 1; ii++)
   {
      ret = gst_ciscdemux_flush(demux, demux->srcpad_discrete[ii]);
      if(TRUE != ret)
      {
         break;
      }
   }

   }while(0);

   return ret;
}

/* entry point to initialize the plug-in
* initialize the plug-in itself
* register the element factories and other features
*/
static gboolean
ciscdemux_init (GstPlugin * ciscdemux)
{
   gboolean value = TRUE;
   /* debug category for fltering log messages
   *
   */
   GST_DEBUG_CATEGORY_INIT (gst_ciscdemux_debug, "ciscdemux", 0, "Cisco HLS Demuxer");

   value = gst_element_register (ciscdemux, "cischlsdemux", GST_RANK_PRIMARY + 5, GST_TYPE_CISCDEMUX);

   gst_type_find_register(ciscdemux, "application/x-hlss", GST_RANK_PRIMARY, hls_type_find, NULL, ABR_CAPS, NULL, NULL);

   return value;
}

/* gstreamer looks for this structure to register ciscdemux
*
*/
GST_PLUGIN_DEFINE (
                   GST_VERSION_MAJOR,
                   GST_VERSION_MINOR,
#if GST_CHECK_VERSION(1,0,0)
                   ciscdemux,
#else
                   "ciscdemux",
#endif
                   "Cisco HLS Gstreamer plugin",
                   ciscdemux_init,
                   VERSION,
                   "LGPL",
                   "GStreamer",
                   "http://gstreamer.net/"
                   )
