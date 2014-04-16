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
    GST_STATIC_CAPS ("application/x-hlss")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY );

GST_BOILERPLATE (Gstciscdemux, gst_ciscdemux, GstElement, GST_TYPE_ELEMENT);

static void gst_ciscdemux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ciscdemux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_ciscdemux_chain (GstPad * pad, GstBuffer * buf);
static GstPadLinkReturn gst_ciscdemux_src_link(GstPad *pad, GstPad *peer);
static GstStateChangeReturn gst_cscohlsdemuxer_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_cscohlsdemuxer_sink_event (GstPad * pad, GstEvent * event);
static GstClockTime gst_cisco_hls_get_duration (Gstciscdemux *demux);
static gboolean cisco_hls_initialize (Gstciscdemux *demux);
static gboolean cisco_hls_open(Gstciscdemux *demux);
static gboolean cisco_hls_start(Gstciscdemux *demux, char *pPlaylistUri);
static gboolean cisco_hls_close(Gstciscdemux *demux);
static gboolean cisco_hls_finalize(Gstciscdemux *demux);
static gboolean gst_cisco_hls_seek (Gstciscdemux *demux, GstEvent *event);
static void * getCurrentPTSNotify(void *data);
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
            base = sprintf(p, "%02x", (s[i]& 0xFF));
            p += base;
         }
         

         p = strIv;
         s = (char*) metadata->iv;
         // each unsigned char is 2 characters.
         
         for (i =0; i< 16; i++)
         {
            base = sprintf(p, "%02x", (s[i] & 0xFF));
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
      gst_static_pad_template_get (&sink_factory));
}

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

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_ciscdemux_init (Gstciscdemux * demux,
    GstciscdemuxClass * gclass)
{

   /* sink pad*/
  demux->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  demux->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  /* Sink pad chain function set*/
  gst_pad_set_chain_function (demux->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_ciscdemux_chain));
   
  gst_pad_set_event_function (demux->sinkpad,
                              GST_DEBUG_FUNCPTR (gst_cscohlsdemuxer_sink_event ));

  gst_pad_set_link_function(demux->srcpad, GST_DEBUG_FUNCPTR (gst_ciscdemux_src_link));

  /* now enable the pads*/
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
  gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad);

  demux->silent = FALSE;
  demux->capsSet =0;
  demux->bKillPTSThread = FALSE;
  demux->playerEvtCb = NULL;
  demux->downstream_peer_pad = NULL;
  
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
gst_ciscdemux_chain (GstPad * pad, GstBuffer * buf)
{
  Gstciscdemux *demux;
  demux = GST_CISCDEMUX (GST_OBJECT_PARENT (pad));

   gst_buffer_unref(buf);
  /* just push out the incoming buffer without touching it */
   return GST_FLOW_OK;
}

static GstPadLinkReturn gst_ciscdemux_src_link(GstPad *pad, GstPad *peer) 
{
   GstElement *pElement;
   Gstciscdemux *demux = GST_CISCDEMUX(gst_pad_get_parent(pad));

   gchar *peerName = gst_pad_get_name(peer);
   pElement = (GstElement*) gst_pad_get_parent_element (pad); 
   GST_WARNING("---->[%s]Src Link with peer %s (Element %s)  %s (Element ?)(Pad %p %p)\n", 
               __FUNCTION__, gst_pad_get_name(pad),GST_ELEMENT_NAME(pElement), peerName,pad,peer);

   pElement = (GstElement*) gst_pad_get_parent_element (peer); 
   if(pElement)
   {
      GST_WARNING("Peer Element: %s\n", GST_ELEMENT_NAME(pElement));
   }

   g_free(peerName);

   if (GST_PAD_LINKFUNC (peer)) 
   {
      GST_PAD_LINKFUNC (peer) (peer, pad);
   }    

   demux->downstream_peer_pad = peer;
   return GST_PAD_LINK_OK;
}     


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
ciscdemux_init (GstPlugin * ciscdemux)
{
   gboolean value =TRUE;
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_ciscdemux_debug, "ciscdemux",
      0, "Cisco HLS Demuxer");


  value = gst_element_register (ciscdemux, "cischlsdemux", GST_RANK_PRIMARY + 5,
      GST_TYPE_CISCDEMUX);
  gst_type_find_register(ciscdemux, "application/x-hlss",GST_RANK_PRIMARY,hls_type_find,NULL,ABR_CAPS,NULL,NULL); 


  return value;
}


/* gstreamer looks for this structure to register ciscdemuxs
 *
 * exchange the string 'Template ciscdemux' with your ciscdemux description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cischlsdemux",
    "Cisco HLS Gstreamer plugin ",
    ciscdemux_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)

static gboolean gst_cscohlsdemuxer_src_query (GstPad * pad, GstQuery * query)
{
  Gstciscdemux *demux = GST_CISCDEMUX (gst_pad_get_parent(pad));
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;


  switch (query->type) 
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
     default:
        /* Don't forward queries upstream because of the special nature of this
         * "demuxer", which relies on the upstream element only to be fed with the
         * first playlist */
        break;
  } //end of switch

  return ret;
}

static gboolean gst_cscohlsdemuxer_src_event (GstPad * pad, GstEvent * event)
{
   Gstciscdemux *demux = GST_CISCDEMUX (gst_pad_get_parent(pad));
   gboolean res = FALSE;

   switch (event->type)
   {
      case GST_EVENT_SEEK:
      {
         GST_WARNING("Got seek event!");
         res = gst_cisco_hls_seek (demux, event);
         gst_event_unref(event);
         return res;
      }
      default: 
         break;
   }//end of switch

   return gst_pad_event_default(pad,event);
}

static gboolean gst_cscohlsdemuxer_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  gchar *uri;
  GstQuery *query;
  Gstciscdemux *demux = GST_CISCDEMUX (gst_pad_get_parent(pad));


  switch (event->type)
  {
      case GST_EVENT_EOS:
         GST_WARNING_OBJECT(demux, "Got EOS for the playlist");
         /* This means that the source was able to retreive the m3u8 url playlist.
          * we need to get this url and pass it to the cisco HLS plugin.
          */
         query = gst_query_new_uri();
         ret = gst_pad_peer_query(demux->sinkpad,query);
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
               if (demux->LicenseID != NULL){g_free(demux->LicenseID);}
               demux->LicenseID = strdup (array[0]);
               g_strfreev(array);


            }while(0);
            // we can now set the uri with the cisco plugin.
            cisco_hls_start(demux, uri); 
            
            //ok free the uri string
            g_free(uri);
         }
         else
         {

            GST_WARNING_OBJECT(demux, "This should not have happened, I'm unable to get the url from pipeline. NOT starting HLS");
            gst_event_unref(event);
            break;
         }
         gst_event_unref(event);
         return TRUE;
      case GST_EVENT_NEWSEGMENT:
         // ignor these
         gst_event_unref(event);
         return TRUE;
      default:
         break;
  }//end of switch
  return gst_pad_event_default (pad, event);
}
static GstStateChangeReturn gst_cscohlsdemuxer_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  Gstciscdemux *demux = GST_CISCDEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      cisco_hls_initialize (demux);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      cisco_hls_open(demux);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* Start the streaming loop in paused only if we already received
         the main playlist. It might have been stopped if we were in PAUSED
         state and we filled our queue with enough cached fragments
       */
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
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

   GST_LOG(" Entered: %s : got event [%15s] callback for pHandle: %p : %d\n",
         __FUNCTION__, strHLSEvent[pEvt->eventCode],pHandle, pEvt->eventCode);

   if (demux == NULL)
      return;

   switch( pEvt->eventCode )
   {
      case SRC_PLUGIN_EOF:
         if (demux->srcpad != NULL)
         {
            GstEvent *event = gst_event_new_eos ();
            if (gst_pad_push_event (demux->srcpad, event) == FALSE)
            {
               GST_WARNING("Error sending the eos event down stream\n");
            }
         }
         break;
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


   //printf("Entered: %s\n",__FUNCTION__);
   do
   {
      // we need to rate limit a bit
      pmem = (tMemoryStruct*) malloc(sizeof(tMemoryStruct));

      pmem->buf = gst_buffer_try_new_and_alloc(4096);
      if (G_UNLIKELY (pmem->buf == NULL )) 
      {
         GST_ERROR("Error Getting memory from gst_buffer_try_new_and_alloc\n");
      }
      pmem->memory =GST_BUFFER_DATA(pmem->buf);
//TODO Make this buffer the size of the pipeline buffer
      pmem->size = 4096;
      pmem->bInUse = TRUE;

      *ppPrivate = (void*) pmem;
      *size = pmem->size;
      *buffer = pmem->memory;

      memset( GST_BUFFER_DATA (pmem->buf), 0, pmem->size  );


   }while (0);

   return status;
}
srcStatus_t hlsPlayer_sendBuffer(void* pHandle, char* buffer, int size, srcBufferMetadata_t* metadata, void *pPrivate)
{
   srcStatus_t status = SRC_SUCCESS;
   tMemoryStruct *pmem = NULL;
   GstBuffer *buf = NULL;
   GstEvent*event= NULL;
   GstCaps *caps =NULL;

   // send it downstream first we need to see if we need to set the capabilities
   Gstciscdemux *demux = (Gstciscdemux*) pHandle;


   do
   {

      if (pPrivate == NULL)
      {
         GST_ERROR("Warning %s pPrivate is NULL\n", __FUNCTION__);
         break;
      }
      pmem = (tMemoryStruct*)pPrivate;
      pmem->size = size ; // this is the ACTUAL data amount written to the buffer, the size of the actual
      // buffer is still the original 1500 bytes

      buf = pmem->buf;
      GST_BUFFER_SIZE(buf) = size;

      // we don't need the pPrivate anymore
      free(pmem);

      // The first packet should always have meta data with it, if not we have an error

      if ((metadata == NULL) && (demux->capsSet == 0))
      {
         GST_ERROR("the Capabilities are not set and the metadata field was null This should NEVER happen\n");
      }

      // check to see if we have setup the source pad yet to send out it's capabilities
      if (demux->capsSet ==0)
      {
         if (metadata)
         {
            // this is for basic HLS
            if (metadata->encType == SRC_ENC_AES128_CBC)
            {

               #ifdef OPT_FORCE_VGDRM
                  caps = gst_caps_new_simple ("drm/x-VGDRM", NULL);
               #elif OPT_FORCE_VERIMATRIX
                  caps = gst_caps_new_simple ("drm/x-VERIMATRIX", NULL);
               #else
                  caps = gst_caps_new_simple ("drm/x-BASIC_HLS", NULL);
               #endif
            }
            else if ( metadata->encType == SRC_ENC_NONE)
            {
               caps =gst_type_find_helper_for_buffer(NULL, buf, NULL);
            }
            else
            {
               caps = NULL;
               GST_WARNING("I don't know this encryption type\n");
            }

         }// end of if metadata
         GST_INFO_OBJECT(demux, "Capabilities: %" GST_PTR_FORMAT,caps);
         demux->inputStreamCap = caps;
         gst_pad_set_event_function(demux->srcpad,
               GST_DEBUG_FUNCPTR(gst_cscohlsdemuxer_src_event));

         gst_pad_set_query_function (demux->srcpad,
               GST_DEBUG_FUNCPTR (gst_cscohlsdemuxer_src_query));

         gst_pad_set_element_private (demux->srcpad, demux);
         if (FALSE == gst_pad_set_active (demux->srcpad, TRUE))
         {
            GST_WARNING_OBJECT(demux,"I was not able to set the src pad to active\n!"); 
         }

         if (FALSE == gst_pad_set_caps (demux->srcpad,  demux->inputStreamCap))
         {
            GST_WARNING_OBJECT(demux, "I was not able to set caps on the src pad for some reason\n");
         }
         GST_INFO_OBJECT(demux, "demux->srcpad: Capabilities: %" GST_PTR_FORMAT,gst_pad_get_caps(demux->srcpad));
         demux->capsSet =1;


         pthread_create(&demux->getPTSThread, NULL, getCurrentPTSNotify, demux);


      }// end of if capsSet


      // Now that the pad is setup and the capabilities set on the pad we can send a decryption event
      // if necessary or we can just send the buffer down the pipeline this is outside the above block 
      // because you can get a new key tag at any time
      if (metadata)
      {
         if (metadata->encType ==SRC_ENC_AES128_CBC)
         {
            // now we can send the event downstream. :-)
            event = gst_ciscdemux_create_decryption_event(metadata,demux); 
            if (event == NULL) { GST_ERROR("Error no event to send\n");}

            GST_INFO_OBJECT(demux, "Sending the encryption key information downstream\n");
            if (gst_pad_push_event(demux->srcpad, event)== FALSE)
            {
               GST_WARNING(" Error sending the encyption key down stream\n");
            }
         }// end of if metadata->SRC_ENC_AES128CBC
      } // end of if metadata

      gst_buffer_set_caps(buf, demux->inputStreamCap);
      gst_pad_push(demux->srcpad,buf);

   }while(0);

   return status;
}
srcStatus_t hlsPlayer_set(void *pHandle, srcPlayerSetData_t *pSetData)
{
   srcStatus_t status = SRC_SUCCESS;

   Gstciscdemux *demux = (Gstciscdemux *)pHandle;   

   if (demux == NULL)
      return SRC_ERROR;
   do
   {
      if (pSetData->setCode == SRC_PLAYER_SET_BUFFER_FLUSH)
      {
         GstEvent *event;

         if (demux->srcpad == NULL)
         {
            GST_ERROR("cisco demux source pad not linked!\n");
            status = SRC_ERROR;
            break;
         }

         event = gst_event_new_flush_start ();
         if (event == NULL)
         {
            status = SRC_ERROR;
            break;
         }

         GST_LOG("cisco demux sending flush start downstream...\n");
         gst_pad_push_event (demux->srcpad, event);

         event = gst_event_new_flush_stop ();
         if (event == NULL)
         {
            status = SRC_ERROR;
            break;
         }

         GST_LOG("cisco demux sending flush stop downstream...\n");
         gst_pad_push_event (demux->srcpad, event);
      }
   }while (0);

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

static gboolean cisco_hls_open (Gstciscdemux *demux)
{
   srcStatus_t stat = SRC_SUCCESS;
   srcPluginErr_t errTable;
   tSession *pSession = NULL;
   gboolean bError = FALSE;

   do 
   {
      demux->pCscoHlsSession = pSession = (tSession *) malloc(sizeof(tSession));
      pSession->pHandle_Player = demux;
      stat = demux->HLS_pluginTable.open(&pSession->pSessionID, pSession->pHandle_Player, &errTable);
      if (stat != SRC_SUCCESS)
      {
         GST_ERROR(" Hummm there was an error opening the HLS plugin:%s\n", errTable.errMsg);
         bError =TRUE;
         break;
      }
      else
      {
         GST_LOG("Done opening the HLS callbacks\n"); 
      }
   }while(0);

   return bError;
}

static gboolean cisco_hls_start(Gstciscdemux *demux, char *pPlaylistUri)
{
   srcStatus_t stat = SRC_SUCCESS;
   srcPluginErr_t errTable;
   srcPluginSetData_t setData;
   tSession *pSession = demux->pCscoHlsSession;

   // First save the uri to our local object
   if (demux->uri){g_free(demux->uri);}
   if (gst_uri_is_valid(pPlaylistUri))
   {
      demux->uri = strndup(pPlaylistUri, 512);
   }
   else
   {
      GST_WARNING_OBJECT(demux, "Passed in URI is NOT valid");
   }

   //Now set the uri with the cisco hls plugin and let's kick it off.
   setData.setCode = SRC_PLUGIN_SET_DATA_SOURCE;
   setData.pData = strndup(demux->uri,512 );

   GST_LOG("Setting location to : %s \n",(char*) setData.pData);

   /* now set the location of the m3u8 url to the HLS plugin */
   stat = demux->HLS_pluginTable.set(pSession->pSessionID, &setData, &errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error setting the url to the HLS plugin:%s\n", errTable.errMsg);
   }
   free(setData.pData);

/* prepare */
   stat = demux->HLS_pluginTable.prepare(pSession->pSessionID,&errTable );
   if(stat)
   {
      GST_ERROR( "%s: Error %d while preparing playlist: %s", __FUNCTION__, errTable.errCode, errTable.errMsg);
      return FALSE;
   }

   setData.setCode = SRC_PLUGIN_SET_SPEED;
   setData.pData = malloc(sizeof(float));
   *(float *)(setData.pData) = 1;
   /* setSpeed (play) */
   stat = demux->HLS_pluginTable.set( pSession->pSessionID, &setData, &errTable );
   if(stat)
   {
      GST_ERROR( "%s: Error %d while setting speed to 1: %s", __FUNCTION__, errTable.errCode, errTable.errMsg);
      free(setData.pData);
      return FALSE;
   }
   free(setData.pData);
   setData.pData = NULL;


   return TRUE;

}

static gboolean cisco_hls_close(Gstciscdemux *demux)
{
   srcPluginErr_t errTable;
   srcStatus_t stat = SRC_SUCCESS;

   pthread_mutex_lock(&demux->PTSMutex);
   demux->bKillPTSThread = TRUE;
   pthread_mutex_unlock(&demux->PTSMutex);
   
   pthread_join(demux->getPTSThread, NULL);

   stat = demux->HLS_pluginTable.close(demux->pCscoHlsSession->pSessionID, &errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error closing the HLS plugin:%s\n", errTable.errMsg);
   }
   else
   {
      GST_LOG("Done closing the HLS plugin session\n"); 
   }

   free(demux->pCscoHlsSession);
   if(demux->LicenseID){g_free(demux->LicenseID);}
   demux->pCscoHlsSession = NULL;
   
   return TRUE;
}

static gboolean cisco_hls_finalize(Gstciscdemux *demux)
{
   srcPluginErr_t errTable;
   srcStatus_t stat = SRC_SUCCESS;

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
   getData.pData = malloc(sizeof(float));

   /* now set the location of the m3u8 url to the HLS plugin */
   stat = demux->HLS_pluginTable.get(pSession->pSessionID, &getData, &errTable);
   if (stat != SRC_SUCCESS)
   {
      GST_ERROR(" Hummm there was an error obtaining the duration: %s\n", errTable.errMsg);
   }
   
   t = *((float *)getData.pData);
   t = t * GST_MSECOND; // turn ms into gstreamer time base
   free(getData.pData);

   GST_LOG("[cischlsdemux] - duration = %llu\n", t);
   // returned value is in nanoseconds.
  return t; 
}

static gboolean gst_cisco_hls_seek (Gstciscdemux *demux, GstEvent *event)
{
   gfloat position;
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
      GST_ERROR("[cischlsdemux] - HLS session not opened!\n");
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

   gst_event_parse_seek (event, &rate, &format, &flags, &curType, &cur, &stopType, &stop);

   setData.setCode = SRC_PLUGIN_SET_POSITION;
   position = (gfloat)(cur / GST_MSECOND);
   setData.pData = &position;
   GST_WARNING("[cischlsdemux] seeking to position %f, timestamp %llu...\n", position, cur);
   stat = demux->HLS_pluginTable.set(pSession->pSessionID, &setData, &errTable);
   if ( stat == SRC_ERROR )
   {
      GST_WARNING("Failed to set position on the source plugin: %s\n", errTable.errMsg);
      return FALSE;
   }  

   return TRUE;
}

static void * getCurrentPTSNotify(void *data)
{
   GstQuery        *query = NULL;
   GstStructure    *structure;
   const GValue    *val = NULL;
   gpointer        *ptr = NULL;
   gboolean        rc;
   gint64          pts = 0;
   Gstciscdemux    *demux = (Gstciscdemux *)data;
   srcPlayerEvt_t  ptsEvent = {SRC_PLAYER_LAST_PTS, NULL};
   struct timespec ts = {};
   
   GST_LOG("%s() >>>>\n", __FUNCTION__);

   if(NULL == demux)
   {
      GST_WARNING("demux is NULL\n");
      goto fn_exit;
   }
   if(NULL == demux->pCscoHlsSession)
   {
      GST_WARNING("libhls session is NULL\n");
      goto fn_exit;
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
      query = gst_query_new_application(GST_QUERY_CUSTOM, structure);
      rc = gst_pad_query(demux->downstream_peer_pad, query);
      if (!rc) 
      {
         GST_WARNING("could not get pts");
         continue;
      }    

      structure = gst_query_get_structure(query);
      val = gst_structure_get_value(structure, "current_pts");
      if (val == NULL) 
      {
         GST_WARNING("could not get pts");
         continue;
      }    

      ptr = g_value_get_pointer(val);
      if(NULL == ptr)
      {
         GST_WARNING("pts value is 0\n");
         continue;
      }

      pts = 0;
      memcpy((gchar *)&pts, (gchar *)&ptr, sizeof(ptr)); 

      GST_DEBUG("Current PTS %lld\n", pts);

      gst_query_unref(query);

      if(NULL != demux->playerEvtCb)
      {
            ptsEvent.pData = &pts;
            demux->playerEvtCb(demux->pCscoHlsSession->pSessionID, &ptsEvent);
      }
      else
      {
         GST_WARNING("playerEvtCb is NULL\n");
         break;
      }
   }while(1);

fn_exit:
   GST_LOG("%s() <<<<\n", __FUNCTION__);
   pthread_exit(NULL);
}
