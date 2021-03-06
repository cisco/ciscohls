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
#define INVALID_PTS  -1
#define BUFFER_SIZE 65536
/* Convert from GStreamer time to MPEG time. */
#define GSTTIME_TO_MPEGTIME(time) (((time) * 9) / (GST_MSECOND/10))

/* demux signals and args */
enum
{
   /* FILL ME */
   LAST_SIGNAL
};

enum
{
   PROP_0,
   PROP_SILENT,
   PROP_AUDIO_LANGUAGE
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
static gboolean gst_ciscdemux_get_caps( Gstciscdemux *demux,
												srcBufferMetadata_t *metadata,
                                       			GstBuffer *buf,
                                       			GstCaps **caps);
static gboolean gst_ciscdemux_flush(Gstciscdemux *demux, GstPad *srcpad);
static gboolean gst_ciscdemux_disable_main_stream_audio(Gstciscdemux *demux);
static gboolean gst_ciscdemux_set_video_master(Gstciscdemux *demux);
static gboolean gst_ciscdemux_set_audio_thresholds(Gstciscdemux *demux);
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

		if (demux->drmType)
		{
	  		if((g_strstr_len (demux->drmType, strlen(DRM_TYPE_VERIMATRIX), DRM_TYPE_VERIMATRIX) != NULL)
				|| (g_strstr_len (demux->drmType, strlen(DRM_TYPE_BASIC), DRM_TYPE_BASIC) != NULL))
			{
		         GST_DEBUG("Forcing DRM type to: \"%s\"\n", demux->drmType);
		         structure = gst_structure_new(	demux->drmType,
									            "keyURI", G_TYPE_STRING, metadata->keyURI,
									            "iv",     G_TYPE_STRING, strIv,
									            NULL);
			}
			else if(g_strstr_len (demux->drmType, strlen(DRM_TYPE_VGDRM), DRM_TYPE_VGDRM) != NULL)

			{
				GST_DEBUG("Forcing DRM type to: \"%s\"\n", demux->drmType);
	         	structure = gst_structure_new(	demux->drmType,
									            "keyURI",   G_TYPE_STRING, metadata->keyURI,
									            "iv",       G_TYPE_STRING, strIv,
									            "LicenseID",G_TYPE_STRING, demux->LicenseID,
									            NULL);
			}
			else
			{
	            GST_ERROR("demux->drmType should NOT be NULL at this point!\n");
			}
		}
		else
		{
			GST_ERROR("Unknown DRM type!\n");
		}

         if (structure == NULL)
         {
            GST_ERROR("Error creating event message\n");
         }
         else
         {
            GST_DEBUG("sending out DRM info: %s \n", gst_structure_get_name(structure));
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
    "Codec/Demuxer/Adaptive",
    "Cisco's HLS library GStreamer interface",
     //leading whitespace below after the first author is to align      
     //the gst-inspect output nicely, don't remove them 
    "Matt Snoby <<snobym@cisco.com>>\n\
                         Saravanakumar Periyaswamy <<sarperiy@cisco.com>>\n\
                         Tankut Akgul <<akgult@cisco.com>>"
    );
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

   g_object_class_install_property (gobject_class, PROP_AUDIO_LANGUAGE,
      g_param_spec_string ("audio-language", "Audio language ISO code",
          "Sets audio language", NULL, G_PARAM_READWRITE));

   gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cscohlsdemuxer_change_state);

#if GST_CHECK_VERSION(1,0,0)
   gst_element_class_set_details_simple(gstelement_class,
      "Cisco HLS Demuxer",
      // The class metadata line below MUST include the keywords Demuxer and Adaptive.
      // This tells playbin/decodebin to dynamically adjust multiqueue sizes it inserts
      // int the pipeline and also makes autoplugging function correctly.
      "Codec/Demuxer/Adaptive",
      "Cisco's HLS library GStreamer interface",
      //leading whitespace below after the first author is to align      
      //the gst-inspect output nicely, don't remove them 
      "Matt Snoby <<snobym@cisco.com>>\n\
                           Saravanakumar Periyaswamy <<sarperiy@cisco.com>>\n\
                           Tankut Akgul <<akgult@cisco.com>>");
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
   demux->newSegment = TRUE;
   demux->drmType = NULL;
   demux->defaultAudioLangISOCode[0] = '\0';

   g_mutex_init(&demux->PTSMutex);
   g_cond_init(&demux->PTSThreadCond);
}

static void
gst_ciscdemux_set_property (GObject * object, guint prop_id,
                            const GValue * value, GParamSpec * pspec)
{
   Gstciscdemux *demux = GST_CISCDEMUX (object);
   srcPluginSetData_t setData = {};
   tSession *pSession = demux->pCscoHlsSession;
   char *pAudioLang = NULL;
   srcPluginErr_t errTable;

   switch (prop_id) {
       case PROP_SILENT:
          demux->silent = g_value_get_boolean (value);
          break;

       case PROP_AUDIO_LANGUAGE:
          {
             pAudioLang = g_value_get_string(value);
             if(NULL == pAudioLang)
             {
                GST_ERROR("Invalid audio language ISO code\n");
                break;
             }

             if((NULL == pSession) || (NULL == pSession->pSessionID))
             {
                g_strlcpy(demux->defaultAudioLangISOCode, pAudioLang, sizeof(demux->defaultAudioLangISOCode));
                GST_WARNING("libhls session is NULL, audio lang will be set when libhls session is created\n");
                break;
             }

             GST_WARNING("Setting audio language to %s\n", pAudioLang);

             setData.setCode = SRC_PLUGIN_SET_AUDIO_LANGUAGE;
             setData.pData = pAudioLang;
             if(SRC_SUCCESS != demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable ))
             {
                GST_ERROR( "%s: Error %d while setting audio language to %s: %s",
                      __FUNCTION__, errTable.errCode, pAudioLang, errTable.errMsg);
             }
             break;
          }
       default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
   }
}

static void
gst_ciscdemux_get_property (GObject * object, guint prop_id,
                            GValue * value, GParamSpec * pspec)
{
   Gstciscdemux       *demux = GST_CISCDEMUX (object);
   srcStatus_t        stat = SRC_SUCCESS;
   srcPluginGetData_t getData = {};
   srcPluginErr_t     errTable = {};
   gchar              audioLanguage[ISO_LANG_CODE_LEN + 1] = "";

   switch (prop_id) {
       case PROP_SILENT:
          g_value_set_boolean (value, demux->silent);
          break;
       case PROP_AUDIO_LANGUAGE:
          {
             if((NULL == demux) || (NULL == demux->pCscoHlsSession))
             {
                GST_ERROR("Invalid session handle(s)\n");
                break;
             }

             getData.getCode = SRC_PLUGIN_GET_AUDIO_LANGUAGE;
             getData.pData = audioLanguage;
             stat = demux->HLS_pluginTable.get(demux->pCscoHlsSession->pSessionID, &getData, &errTable);
             if (stat != SRC_SUCCESS)
             {
                GST_ERROR("There was an error obtaining current audio language: %s\n",
                      errTable.errMsg);
                break;
             }

             GST_INFO("current audioLanguage: %s\n", audioLanguage);
             g_value_set_string (value, audioLanguage);

             break;
          }
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
   Gstciscdemux              *demux;
   gboolean                  ret = FALSE;
   srcPluginGetData_t        getData = {};
   int                       bTrickSupported = 0;
   srcPluginContentType_t    contentType;
   gchar                     speeds[64] = "";
   tSession                  *pSession = NULL;
   srcPluginErr_t            errTable = {};
   srcStatus_t               stat = SRC_SUCCESS;
   int                       numAudioLanguages = 0;
   srcPluginAudioLanguages_t audioLanguages = {};
   gint                       ii = 0;
   gchar                     commaSepDiscreteLangISO[64] = "";
   gchar                     commaSepMuxedLangISO[64] = "";
   gint                      isoStrLen = 0;

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

   pSession = demux->pCscoHlsSession;
   if ( pSession == NULL )
   {
      GST_ERROR("libhls session is NULL!\n");
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
         else if(gst_structure_has_name(pStruct, "getAudioLangInfo"))
         {
            GST_WARNING("getAudioLangInfo query\n");

            getData.getCode = SRC_PLUGIN_GET_NUM_AUDIO_LANGUAGES;
            getData.pData = &numAudioLanguages;
            stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
            if (stat != SRC_SUCCESS)
            {
               GST_ERROR("There was an error obtaining number of available audio languages: %s\n",
                     errTable.errMsg);
               break;
            }
            GST_INFO("numAudioLanguages = %d\n", numAudioLanguages);

            if (0 >= numAudioLanguages)
            {
               GST_ERROR("No audio languages available\n");
               ret = FALSE;
               break;
            }
            audioLanguages.numAudioLanguages = numAudioLanguages;
            audioLanguages.audioLangInfoArr =
               (srcPluginAudioLangInfo_t *)g_malloc(sizeof(srcPluginAudioLangInfo_t) * numAudioLanguages);
            if(NULL == audioLanguages.audioLangInfoArr)
            {
               GST_ERROR("Failed to alloc memory for audioLangInfo array\n");
               ret = FALSE;
               break;
            }

            getData.getCode = SRC_PLUGIN_GET_AUDIO_LANGUAGES_INFO;
            getData.pData = &audioLanguages;
            stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
            if (stat != SRC_SUCCESS)
            {
               GST_ERROR("There was an error obtaining number of available audio languages: %s\n",
                     errTable.errMsg);
               g_free(audioLanguages.audioLangInfoArr);
               audioLanguages.audioLangInfoArr = NULL;
               ret = FALSE;
               break;
            }
            GST_DEBUG("numAudioLanguages = %d\n", audioLanguages.numAudioLanguages);

            for(ii = 0; ii < audioLanguages.numAudioLanguages; ii++)
            {
               GST_DEBUG("isoCode[%d]: %s\n", ii, audioLanguages.audioLangInfoArr[ii].isoCode);
               GST_DEBUG("discrete[%d]: %d\n", ii, audioLanguages.audioLangInfoArr[ii].bDiscrete);

               if(1 == audioLanguages.audioLangInfoArr[ii].bDiscrete)
               {
                  g_strlcat(commaSepDiscreteLangISO, audioLanguages.audioLangInfoArr[ii].isoCode,
                        sizeof(commaSepDiscreteLangISO));
                  g_strlcat(commaSepDiscreteLangISO, ",", sizeof(commaSepDiscreteLangISO));
               }
               else
               {
                  g_strlcat(commaSepMuxedLangISO, audioLanguages.audioLangInfoArr[ii].isoCode,
                        sizeof(commaSepMuxedLangISO));
                  g_strlcat(commaSepMuxedLangISO, ",", sizeof(commaSepMuxedLangISO));
               }
            }

            isoStrLen = strlen(commaSepMuxedLangISO);
            if((isoStrLen > 0) && (commaSepMuxedLangISO[isoStrLen - 1] == ','))
            {
               commaSepMuxedLangISO[isoStrLen - 1] = '\0';
            }

            isoStrLen = strlen(commaSepDiscreteLangISO);
            if((isoStrLen > 0) && (commaSepDiscreteLangISO[isoStrLen - 1] == ','))
            {
               commaSepDiscreteLangISO[isoStrLen - 1] = '\0';
            }

            GST_WARNING("commaSepMuxedLangISO: %s\n", commaSepMuxedLangISO);
            GST_WARNING("commaSepDiscreteLangISO: %s\n", commaSepDiscreteLangISO);

            gst_structure_set(pStruct,
                              "commaSepMuxedLangISO", G_TYPE_STRING, commaSepMuxedLangISO,
                              "commaSepDiscreteLangISO", G_TYPE_STRING, commaSepDiscreteLangISO,
                              NULL);

            g_free(audioLanguages.audioLangInfoArr);
            audioLanguages.audioLangInfoArr = NULL;

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

               GST_DEBUG("%s: Received URI = '%s'\n", __func__, uri);

               str =  g_strstr_len (uri, -1, "LicenseID=");
               if (str != NULL)
               {
                  GST_INFO_OBJECT("%s: I found a LicenseID in the string  = '%s'\n", __func__, str);
                  // copy string until you find '&'
                  array = g_strsplit (str, &delimiter, 1024);
                  // the first array element string should hold what we need.
                  GST_INFO_OBJECT(demux,"Setting LicenseID to: %s\n",array[0]);
                  if (demux->LicenseID != NULL)
                  {
                     g_free(demux->LicenseID);
                  }
                  demux->LicenseID = strdup (array[0]);
                  g_strfreev(array);

                  if (!g_strstr_len (uri,-1, "?drmType="))
                  {
                     demux->drmType = strdup(DRM_TYPE_BASIC);
                     GST_INFO_OBJECT(demux, "Set drmType to: %s\n", demux->drmType);
                  }
                  else
                  {
                     if (g_strstr_len (uri,-1, "verimatrix"))
                     {
                        demux->drmType = strdup(DRM_TYPE_VERIMATRIX);
                        GST_INFO_OBJECT(demux, "Set drmType to: %s\n", demux->drmType);
                     }
                     else if (g_strstr_len (uri,-1, "vgdrm"))
                     {
                        demux->drmType = strdup(DRM_TYPE_VGDRM);
                        GST_INFO_OBJECT(demux, "Set drmType to: %s\n", demux->drmType);
                     }
                     else
                     {
                        GST_ERROR("Unknown drmType!\n");
                     }
                  }
               }
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
      {
         gst_ciscdemux_set_audio_thresholds(demux);
         cisco_hls_start(demux);
      }
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
   }
   return;
}

void hlsPlayer_pluginErrCallback(void* pHandle, srcPluginErr_t* pErr)
{
   Gstciscdemux *demux = (Gstciscdemux *)pHandle;

   do
   {
      if(NULL == demux)
      {
         GST_ERROR("pHandle param is NULL\n");
         break;
      }

      if(NULL == pErr)
      {
         GST_ERROR("pErr param is NULL\n");
         break;
      }

      GST_ERROR(" Entered: %s : got event ERROR callback for pHandle: %p : %d\n",
            __FUNCTION__, pHandle, pErr->errCode);

      if(SRC_PLUGIN_ERR_NETWORK == pErr->errCode)
      {
         gst_element_post_message(GST_ELEMENT_CAST(demux),
               gst_message_new_element(GST_OBJECT_CAST(demux),
                  gst_structure_new("extended_notification",
                     "notification", G_TYPE_STRING, "network_error",
                     NULL)));
      }
   }while(0);

   return;
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
      pmem->buf = gst_buffer_new_allocate (NULL, BUFFER_SIZE, NULL);
#else
      pmem->buf = gst_buffer_try_new_and_alloc(BUFFER_SIZE);
#endif
      if (G_UNLIKELY (pmem->buf == NULL))
      {
         g_free (pmem);
         GST_ERROR("Error getting GstBuffer\n");
         return SRC_ERROR;
      }
      //
      // Initialize the offset to 0 otherwise anyone that dereferences
      // could have a problem.
      //
      GST_BUFFER_OFFSET(pmem->buf) = 0;
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
      pmem->size = BUFFER_SIZE;
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
#if GST_CHECK_VERSION(1,0,0)
      gst_buffer_set_size (buf, size);
      GST_BUFFER_PTS (buf) = (demux->bufferPts * GST_MSECOND) / 90;
#else
      GST_BUFFER_SIZE (buf) = size;
      GST_BUFFER_TIMESTAMP (buf) = (demux->bufferPts * GST_MSECOND) / 90;
#endif
      }
      else
      {
#if GST_CHECK_VERSION(1,0,0)
         gst_buffer_set_size (buf, size);
         GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
         GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;
#else
         GST_BUFFER_SIZE (buf) = size;
         GST_BUFFER_TIMESTAMP (buf) =0;
#endif
      }

      // we don't need the pPrivate anymore
      g_free (pmem);

      if((metadata->streamNum > SRC_STREAM_NUM_MAIN) && (NULL == demux->srcpad_discrete[metadata->streamNum - 1]))
      {
         if(TRUE != gst_ciscdemux_get_caps(demux, metadata, buf, &demux->inputStreamCap[metadata->streamNum]))
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
         if(TRUE != gst_ciscdemux_get_caps(demux, metadata, buf, &demux->inputStreamCap[metadata->streamNum]))
         {
            break;
         }

         GST_WARNING_OBJECT(demux, "Main stream Capabilities: %" GST_PTR_FORMAT,
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

         demux->getPTSThread = g_thread_new("pts_thread",
                                            getCurrentPTSNotify,
                                            demux);

         if (NULL == demux->getPTSThread)
         {
            GST_ERROR("Failed to start PTS thread!\n");
         }
         else
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

     if (demux->newSegment == TRUE)
     {
        GstEvent *segmentEvent = NULL;
#if GST_CHECK_VERSION(1,0,0)
        GstSegment segment;
        gst_segment_init(&segment,GST_FORMAT_TIME);
        segment.rate = 1.0;
        segment.applied_rate = 1.0;
        segment.format = GST_FORMAT_TIME;
        segment.position =0;
        segment.start =  demux->seekpos;
        segment.time =  demux->seekpos; //start;
        segment.stop = gst_cisco_hls_get_duration(demux);
        segment.flags = GST_SEGMENT_FLAG_RESET;
        GST_WARNING(" Sending NewSegment message with flag set to reset, setting:\n new position to  %u (ms) \n start %"G_GINT64_FORMAT"  \n stop %"G_GINT64_FORMAT"\n" ,demux->seekpos/ GST_MSECOND,segment.start, segment.stop);

          GST_BUFFER_TIMESTAMP (buf) =demux->seekpos;

        segmentEvent = gst_event_new_segment (&segment);
#else
        segmentEvent = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, demux->seekpos, GST_CLOCK_TIME_NONE, 0);
#endif
        if (NULL != segmentEvent)
        {
           GST_WARNING("[cischlsdemux] sending segment event downstream for timestamp %"G_GINT64_FORMAT"...\n", demux->seekpos);
           if (gst_pad_push_event (demux->srcpad, segmentEvent) != TRUE)
              GST_WARNING("[cischlsdemux] sending segment event downstream failed!");
        }
        demux->newSegment = FALSE;
     }

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
            demux->newSegment = TRUE;
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

               //HLS may switch audio sample rate or codec. Making audio master makes playback
               //very sensitive to these events and can cause glitches. Therefore, we want video
               //to be the master stream for HLS.
               gst_ciscdemux_set_video_master(demux);
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
   srcStatus_t  status = SRC_ERROR;
   gboolean     rc = FALSE;
   GstFormat    gst_format;
   gint64       pos = 0;
   GstQuery     *query = NULL;
   Gstciscdemux *demux = (Gstciscdemux *)pHandle;

   do
   {
      if(NULL == demux)
      {
         GST_ERROR("Invalid pHandle param\n");
         break;
      }

      if(NULL == pGetData)
      {
         GST_ERROR("Invalid parameter\n");
         break;
      }

      switch(pGetData->getCode)
      {
         case SRC_PLAYER_GET_POSITION:
            {
               query = gst_query_new_position(GST_FORMAT_TIME);
               if(NULL == query)
               {
                  GST_ERROR("Creating a position query failed\n");
                  break;
               }

               rc = gst_pad_query(demux->downstream_peer_pad, query);
               if (!rc)
               {
                  GST_ERROR("could not get current position\n");
                  gst_query_unref(query);
                  query = NULL;
                  break;
               }

               gst_query_parse_position(query, &gst_format, &pos);

               if(NULL == pGetData->pData)
               {
                  GST_ERROR("Invalid pData pointer in pGetData param\n");
                  gst_query_unref(query);
                  query = NULL;
                  break;
               }

               *((int *)pGetData->pData) = pos / GST_MSECOND;
               GST_INFO("Current position(millisec) = %d\n",
                     *((int *)pGetData->pData));

               gst_query_unref(query);
               query = NULL;
               status = SRC_SUCCESS;
            }
            break;

         default:
            break;
      }

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
   int minBitrate = 0;

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
      minBitrate = 320000;
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

      if(strlen(demux->defaultAudioLangISOCode) > 0)
      {
         setData.setCode = SRC_PLUGIN_SET_AUDIO_LANGUAGE;
         setData.pData = demux->defaultAudioLangISOCode;
         stat = demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable );
         if(stat)
         {
            GST_ERROR( "%s: Error %d while setting audio language to %s: %s",
                  __FUNCTION__, errTable.errCode, demux->defaultAudioLangISOCode, errTable.errMsg);
         }
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

   g_mutex_lock(&demux->PTSMutex);
   demux->bKillPTSThread = TRUE;
   GST_DEBUG("Timestamp before killing pts thread : %llu\n", (unsigned long long)time(NULL));
   /* Wake up the PTS thread if it is sleeping */
   g_cond_signal(&demux->PTSThreadCond);
   g_mutex_unlock(&demux->PTSMutex);

   if (TRUE == demux->bGetPTSThreadRunning)
   {
      g_thread_join(demux->getPTSThread);
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

   if (demux->pCscoHlsSession)
   {
      g_free(demux->pCscoHlsSession);
   }

   if (demux->LicenseID)
   {
      g_free(demux->LicenseID);
   }

   if (demux->drmType)
   {
      g_free(demux->drmType);
   }

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

   //Unload hls plugin to free up player function table memory
   srcPluginUnload();

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
   g_mutex_clear(&demux->PTSMutex);
   g_cond_clear(&demux->PTSThreadCond);

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
      demux->seekpos = cur;

      setData.setCode = SRC_PLUGIN_SET_POSITION;
      position = (gfloat)(cur / GST_MSECOND);
      setData.pData = &position;
      GST_WARNING("[cischlsdemux] seeking to position %f, timestamp %"G_GINT64_FORMAT"...\n", position, cur);
      stat = demux->HLS_pluginTable.set(pSession->pSessionID, &setData, &errTable);
      if ( stat == SRC_ERROR )
      {
         GST_WARNING("Failed to set position on the source plugin: %s\n", errTable.errMsg);
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

      GST_LOG("SRC_PLUGIN_SET_SPEED returned\n");
   }

   demux->speed = speed;

   return TRUE;
}
static gboolean platform_getPTS(Gstciscdemux *demux, gint64 *in_pts_90khz)
{
   gboolean bGotPTS = FALSE;
   gboolean rc = FALSE;
   unsigned int    pts_45khz = 0;
   GstQuery     *query = NULL;
   GstFormat    gst_format;
   gint64       pos = 0;

   *in_pts_90khz = INVALID_PTS;

   do
   {

         query = gst_query_new_position(GST_FORMAT_TIME);
         rc = gst_pad_query(demux->downstream_peer_pad, query);
         if (!rc)
         {
            GST_WARNING("Query failed on downstream per pad ");
            gst_query_unref(query);
            bGotPTS = FALSE;
            break;
         }
         gst_query_parse_position(query, &gst_format, &pos);

         if ( pos== NULL)
         {
            GST_WARNING("qst_query_parse_position returned NULL - not able to get position from downstream peer");
            gst_query_unref(query);
            query = NULL;
            bGotPTS = FALSE;
            break;
         }

         // pos is in nanoseconds
         // need to convert to ticks
         // 1khz = 1 ms
         // 90 khz clock = 0.0111111ms

         *in_pts_90khz= GSTTIME_TO_MPEGTIME(pos);


         GST_LOG("90khz PTS %"G_GINT64_MODIFIER"d pos time: %"G_GINT64_MODIFIER"d\n",
               *in_pts_90khz,
               (pos/GST_SECOND));

         gst_query_unref(query);
         bGotPTS = TRUE;
   }while (0);


   return bGotPTS;
}
static void * getCurrentPTSNotify(void *data)
{
   gboolean        rc;
   unsigned int    pts_45khz = 0;
   gint64          pts_90khz = 0;
   Gstciscdemux    *demux = (Gstciscdemux *)data;
   srcPlayerEvt_t  ptsEvent = {SRC_PLAYER_LAST_PTS, NULL};
   GTimeVal        ts = {0, 0};

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
         g_mutex_lock(&demux->PTSMutex);

         if(TRUE == demux->bKillPTSThread)
         {
            g_mutex_unlock(&demux->PTSMutex);
            break;
         }

         ts.tv_sec = time(NULL) + SRC_PTS_NOTIFY_INTERVAL;
         g_cond_timed_wait(&demux->PTSThreadCond, &demux->PTSMutex, &ts);

         if(TRUE == demux->bKillPTSThread)
         {
            g_mutex_unlock(&demux->PTSMutex);
            break;
         }

         g_mutex_unlock(&demux->PTSMutex);

         if(NULL == demux->downstream_peer_pad)
         {
            /* Try again - srcpad is not linked */
            continue;
         }
         if (FALSE == platform_getPTS(demux, &pts_90khz))
         {
            GST_LOG("Not able to get PTS \n");
            continue;
         }

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

   return NULL;
}

static gboolean gst_ciscdemux_get_caps( Gstciscdemux *demux,
												srcBufferMetadata_t *metadata,
		                                        GstBuffer *buf,
		                                        GstCaps **caps)
{
   gboolean ret = FALSE;

   do
   {
      if((NULL == metadata) || (NULL == buf) || (NULL == caps))
      {
         GST_ERROR("Invalid param\n");
         break;
      }

      // this is for basic HLS
      if (metadata->encType == SRC_ENC_AES128_CBC)
      {
         if (demux->drmType)
         {
            GST_DEBUG("%s: demux->drmType: %s", __func__, demux->drmType);
            if(g_strstr_len (demux->drmType, strlen(DRM_TYPE_VGDRM), DRM_TYPE_VGDRM) != NULL)
            {
               *caps = gst_caps_new_simple ("drm/x-VGDRM", NULL, NULL);
            }
            else if(g_strstr_len (demux->drmType, strlen(DRM_TYPE_VERIMATRIX), DRM_TYPE_VERIMATRIX) != NULL)
            {
               *caps = gst_caps_new_simple ("drm/x-VERIMATRIX", NULL, NULL);
            }
            else if(g_strstr_len (demux->drmType, strlen(DRM_TYPE_BASIC), DRM_TYPE_BASIC) != NULL)
            {
               *caps = gst_caps_new_simple ("drm/x-BASIC_HLS", NULL, NULL);
            }
         }
         else
         {
            GST_ERROR("demux->drmType should not be NULL at this point!");
         }
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
   gboolean ret = FALSE;

   do
   {
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
      event = gst_event_new_flush_stop (TRUE); //rms changed from FALSE
#else
      event = gst_event_new_flush_stop ();
#endif
      if (event == NULL)
      {
         break;
      }

      GST_LOG("cisco demux sending flush stop downstream...\n");
      gst_pad_push_event (srcpad, event);

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
         gst_structure_free(structure);
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

static gboolean gst_ciscdemux_set_video_master(Gstciscdemux *demux)
{
   GstEvent     *event = NULL;
   GstStructure *structure = NULL;
   gboolean     ret = FALSE;

   do
   {
      structure = gst_structure_new("set-video-master", NULL);
      if(NULL == structure)
      {
         GST_ERROR("Error creating set-video-master structure\n");
         break;
      }

      event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
      if(NULL == event)
      {
         gst_structure_free(structure);
         GST_ERROR("Error creating set-video-master event\n");
         break;
      }

      GST_INFO("Sending set-video-master event downstream\n");

      if (gst_pad_push_event(demux->srcpad, event)== FALSE)
      {
         GST_ERROR(" Error sending set-video-master event downstream\n");
         break;
      }

      ret = TRUE;
   }while(0);

   return ret;
}

static gboolean gst_ciscdemux_set_audio_thresholds( Gstciscdemux *demux )
{
   GstEvent     *event = NULL;
   GstStructure *structure = NULL;
   gboolean     ret = FALSE;

   do
   {
      structure = gst_structure_new("set-audio-thresholds",
                                    "gaThreshold", G_TYPE_UINT, 50,           // Set GA threshold to 50ms to prevent audio PTS errors during bitrate changes
                                    "discardThreshold", G_TYPE_UINT, 0,       // Default value
                                    "wideGaThreshold", G_TYPE_BOOLEAN, FALSE, // Default value
                                    NULL);
      if(NULL == structure)
      {
         GST_ERROR("Error creating set-audio-thresholds\n");
         break;
      }

      event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
      if(NULL == event)
      {
         gst_structure_free(structure);
         GST_ERROR("Error creating set-audio-thresholds\n");
         break;
      }

      GST_INFO("Sending set-audio-thresholds event downstream\n");

      if (gst_pad_push_event(demux->srcpad, event)== FALSE)
      {
         GST_ERROR("Error sending set-audio-thresholds event downstream\n");
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
      gst_structure_free(structure);
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
      gst_structure_free(structure);
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
   for(ii = 0; ii < demux->numSrcPadsActive - 1; ii++)
   {
      gst_event_ref(event);
      ret = gst_pad_push_event(demux->srcpad_discrete[ii], event);
      if(TRUE != ret)
      {
         GST_ERROR("Error sending event : %s\n", GST_EVENT_TYPE_NAME(event));
         break;
      }
   }

   ret = gst_pad_push_event(demux->srcpad, event);
   if(TRUE != ret)
   {
      GST_ERROR("Error sending event : %s\n", GST_EVENT_TYPE_NAME(event));
      break;
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
