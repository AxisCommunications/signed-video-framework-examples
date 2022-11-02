/**
 * MIT License
 *
 * Copyright (c) 2021 Axis Communications AB
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next paragraph) shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * SECTION:element-signing
 *
 * Add SEI nalus containing signatures for authentication.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstsigning.h"
#include "gstsigning_defines.h"
#include <signed-video-framework/signed_video_common.h>
#include <signed-video-framework/signed_video_openssl.h>
#include <signed-video-framework/signed_video_sign.h>

GST_DEBUG_CATEGORY_STATIC(gst_signing_debug);
#define GST_CAT_DEFAULT gst_signing_debug

struct _GstSigningPrivate {
  signed_video_t *signed_video;
  GstClockTime last_pts;
};

#define TEMPLATE_CAPS \
  GST_STATIC_CAPS( \
      "video/x-h264, alignment=au; " \
      "video/x-h265, alignment=au")

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, TEMPLATE_CAPS);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, TEMPLATE_CAPS);

G_DEFINE_TYPE_WITH_PRIVATE(GstSigning, gst_signing, GST_TYPE_BASE_TRANSFORM);

static void
gst_signing_finalize(GObject *object);
static gboolean
gst_signing_start(GstBaseTransform *trans);
static gboolean
gst_signing_stop(GstBaseTransform *trans);
static gboolean
gst_signing_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn
gst_signing_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);
static gboolean
gst_signing_sink_event(GstBaseTransform *trans, GstEvent *event);
static gboolean
setup_signing(GstSigning *signing, GstCaps *caps);
static gboolean
terminate_signing(GstSigning *signing);

static void
gst_signing_class_init(GstSigningClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

  GST_DEBUG_CATEGORY_INIT(
      gst_signing_debug, "signing", 0, "Add SEI nalus containing signatures for authentication");

  transform_class->start = GST_DEBUG_FUNCPTR(gst_signing_start);
  transform_class->stop = GST_DEBUG_FUNCPTR(gst_signing_stop);
  transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_signing_set_caps);
  transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_signing_transform_ip);
  transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_signing_sink_event);

  gst_element_class_set_static_metadata(element_class, "Signed Video", "Formatter/Video",
      "Add SEI nalus containing signatures for authentication.",
      "Signed Video Framework <github.com/AxisCommunications/signed-video-framework-examples>");

  gst_element_class_add_static_pad_template(element_class, &sink_template);
  gst_element_class_add_static_pad_template(element_class, &src_template);

  gobject_class->finalize = gst_signing_finalize;
}

static void
gst_signing_init(GstSigning *signing)
{
  signing->priv = gst_signing_get_instance_private(signing);
  signing->priv->last_pts = GST_CLOCK_TIME_NONE;
}

static void
gst_signing_finalize(GObject *object)
{
  GstSigning *signing = GST_SIGNING(object);

  GST_DEBUG_OBJECT(object, "finalized");
  terminate_signing(signing);

  G_OBJECT_CLASS(gst_signing_parent_class)->finalize(object);
}

static gboolean
gst_signing_start(GstBaseTransform *trans)
{
  GstSigning *signing = GST_SIGNING(trans);
  GstCaps *caps = NULL;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT(signing, "start");
  caps = gst_pad_get_current_caps(GST_BASE_TRANSFORM_SRC_PAD(trans));
  if (caps != NULL) {
    res = setup_signing(signing, caps);
    gst_caps_unref(caps);
  } else {
    GST_DEBUG_OBJECT(signing, "caps not configured yet");
  }

  return res;
}

static gboolean
gst_signing_stop(GstBaseTransform *trans)
{
  GstSigning *signing = GST_SIGNING(trans);

  GST_DEBUG_OBJECT(signing, "stop");
  return terminate_signing(signing);
}

static gboolean
gst_signing_set_caps(GstBaseTransform *trans, G_GNUC_UNUSED GstCaps *incaps, GstCaps *outcaps)
{
  GstSigning *signing = GST_SIGNING(trans);

  GST_DEBUG_OBJECT(signing, "set_caps");
  return setup_signing(signing, outcaps);
}

static GstBuffer *
create_buffer_with_current_time(GstSigning *signing)
{
  GstSigningPrivate *priv = signing->priv;
  GstBuffer *buf = NULL;

  buf = gst_buffer_new();
  GST_BUFFER_PTS(buf) = priv->last_pts;
  return buf;
}

static void
free_nalu_data(gpointer data)
{
  signed_video_nalu_data_free(data);
}

/* Prepend NALUs according to results from Signed Video lib calls. Returns the number of NALUs
 * that were prepended to |current_au|, or -1 on error. */
static gint
prepend_nalus(GstSigning *signing, GstBuffer *current_au)
{
  SignedVideoReturnCode sv_rc = SV_UNKNOWN_FAILURE;
  signed_video_nalu_to_prepend_t nalu_to_prepend = {0};
  gint prepend_count = 0;

  sv_rc = signed_video_get_nalu_to_prepend(signing->priv->signed_video, &nalu_to_prepend);
  while (sv_rc == SV_OK && nalu_to_prepend.prepend_instruction != SIGNED_VIDEO_PREPEND_NOTHING) {
    gpointer data = nalu_to_prepend.nalu_data;
    gsize size = nalu_to_prepend.nalu_data_size;
    GstMemory *prepend_mem;

    // Write size into NALU header. The size value should be the data size, minus the size of the
    // size value itself
    GST_WRITE_UINT32_BE(data, size - sizeof(guint32));

    GST_DEBUG_OBJECT(signing, "create a %" G_GSIZE_FORMAT "bytes nalu to prepend", size);
    prepend_mem = gst_memory_new_wrapped(0, data, size, 0, size, data, free_nalu_data);

    switch (nalu_to_prepend.prepend_instruction) {
      case SIGNED_VIDEO_PREPEND_NALU:
        GST_DEBUG_OBJECT(signing, "prepend nalu to current AU");
        gst_buffer_prepend_memory(current_au, prepend_mem);
        prepend_count++;
        break;
      default:
        GST_FIXME_OBJECT(
            signing, "unsupported prepend instruction %d", nalu_to_prepend.prepend_instruction);
        break;
    }

    sv_rc = signed_video_get_nalu_to_prepend(signing->priv->signed_video, &nalu_to_prepend);
  }

  if (sv_rc != SV_OK) goto get_nalu_failed;

  return prepend_count;

get_nalu_failed:
  GST_ERROR_OBJECT(signing, "signed_video_get_nalu_to_prepend failed");
  return -1;
}

static GstFlowReturn
gst_signing_transform_ip(GstBaseTransform *trans, GstBuffer *buf)
{
  GstSigning *signing = GST_SIGNING(trans);
  GstSigningPrivate *priv = signing->priv;
  guint idx = 0;
  GstMemory *nalu_mem = NULL;
  GstMapInfo map_info;
  gint prepend_count = 0;

  priv->last_pts = GST_BUFFER_PTS(buf);
  // last_pts is an GstClockTime object, which is measured in nanoseconds.
  const gint64 timestamp_usec = (const gint64)(priv->last_pts / 1000);
  const gint64 *timestamp_usec_ptr = priv->last_pts == GST_CLOCK_TIME_NONE ? NULL : &timestamp_usec;

  GST_DEBUG_OBJECT(signing, "got buffer with %d memories", gst_buffer_n_memory(buf));
  while (idx < gst_buffer_n_memory(buf)) {
    SignedVideoReturnCode sv_rc;

    nalu_mem = gst_buffer_peek_memory(buf, idx);

    if (G_UNLIKELY(!gst_memory_map(nalu_mem, &map_info, GST_MAP_READ))) {
      GST_ELEMENT_ERROR(signing, RESOURCE, FAILED, ("failed to map memory"), (NULL));
      goto map_failed;
    }

    // Depending on bitstream format the start code is optional, hence libsigned-video supports
    // both. Therefore, since the start code in the pipeline temporarily may have been replaced by
    // the picture data size this format is violated. To pass in valid input data, skip the first
    // four bytes.
    sv_rc = signed_video_add_nalu_for_signing_with_timestamp(
        signing->priv->signed_video, &(map_info.data[4]), map_info.size - 4, timestamp_usec_ptr);
    if (sv_rc != SV_OK) {
      GST_ELEMENT_ERROR(
          signing, STREAM, FAILED, ("failed to add nalu for signing, error %d", sv_rc), (NULL));
      goto add_nalu_failed;
    }

    prepend_count = prepend_nalus(signing, buf);
    if (prepend_count < 0) {
      GST_ELEMENT_ERROR(signing, STREAM, FAILED, ("failed to prepend nalus"), (NULL));
      goto prepend_nalus_failed;
    }

    gst_memory_unmap(nalu_mem, &map_info);

    idx += prepend_count;  // Move past prepended nalus
    idx++;  // Go to next nalu
  }

  if (prepend_count > 0) {
    // Push an event to produce a message saying SEIs have been added.
    GstStructure *structure = gst_structure_new(
        SIGNING_STRUCTURE_NAME, SIGNING_FIELD_NAME, G_TYPE_STRING, "signed", NULL);
    if (!gst_element_post_message(
            GST_ELEMENT(trans), gst_message_new_element(GST_OBJECT(signing), structure))) {
      GST_ELEMENT_ERROR(signing, STREAM, FAILED, ("failed to push message"), (NULL));
    }
  }
  GST_DEBUG_OBJECT(signing, "push AU with %d nalus", gst_buffer_n_memory(buf));

  return GST_FLOW_OK;

prepend_nalus_failed:
add_nalu_failed:
  gst_memory_unmap(nalu_mem, &map_info);
map_failed:
  return GST_FLOW_ERROR;
}

static void
push_access_unit_at_eos(GstSigning *signing)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM(signing);
  GstBuffer *au = NULL;

  if (signed_video_set_end_of_stream(signing->priv->signed_video) != SV_OK) {
    GST_ERROR_OBJECT(signing, "failed to set EOS");
    goto eos_failed;
  }

  au = create_buffer_with_current_time(signing);
  if (prepend_nalus(signing, au) < 0) {
    GST_ERROR_OBJECT(signing, "failed to prepend nalus");
    goto prepend_failed;
  }

  GST_DEBUG_OBJECT(signing, "push AU at EOS: %" GST_PTR_FORMAT, au);
  gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(trans), au);

  return;

prepend_failed:
  gst_buffer_unref(au);
eos_failed:
  return;
}

static gboolean
terminate_signing(GstSigning *signing)
{
  GstSigningPrivate *priv = signing->priv;

  if (priv->signed_video != NULL) {
    signed_video_free(priv->signed_video);
    priv->signed_video = NULL;
  }

  return TRUE;
}

static gboolean
setup_signing(GstSigning *signing, GstCaps *caps)
{
  GstSigningPrivate *priv = signing->priv;
  GstStructure *structure = NULL;
  const gchar *media_type = NULL;
  SignedVideoCodec codec;
  char *private_key = NULL;
  size_t private_key_size = 0;

  g_assert(caps != NULL);

  if (priv->signed_video != NULL) {
    GST_DEBUG("already set-up");
    return TRUE;
  }

  GST_DEBUG("set up Signed Video with caps %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure(caps, 0);
  media_type = gst_structure_get_name(structure);
  if (!g_strcmp0(media_type, "video/x-h264")) {
    codec = SV_CODEC_H264;
  } else if (!g_strcmp0(media_type, "video/x-h265")) {
    codec = SV_CODEC_H265;
  } else {
    GST_ERROR_OBJECT(signing, "unsupported video codec");
    goto unsupported_codec;
  }

  GST_DEBUG_OBJECT(signing, "create Signed Video object");
  priv->signed_video = signed_video_create(codec);
  if (!priv->signed_video) {
    GST_ERROR_OBJECT(signing, "could not create Signed Video object");
    goto create_failed;
  }
  if (signed_video_generate_private_key(
          SIGN_ALGO_ECDSA, PATH_TO_KEY_FILES, &private_key, &private_key_size) != SV_OK) {
    GST_DEBUG_OBJECT(signing, "failed to generate pem file");
    goto generate_private_key_failed;
  }
  if (signed_video_set_private_key(
          priv->signed_video, SIGN_ALGO_ECDSA, private_key, private_key_size) != SV_OK) {
    GST_DEBUG_OBJECT(signing, "failed to set private key content");
    goto set_private_key_failed;
  }

  // Send properties information to video library.
  if (signed_video_set_product_info(priv->signed_video, "N/A", signed_video_get_version(), "N/A",
          "Signed Video Framework",
          "github.com/axteams-software/signed-video-framework") != SV_OK) {
    GST_ERROR_OBJECT(signing, "failed to set properties");
    goto product_info_failed;
  }

  return TRUE;

product_info_failed:
set_private_key_failed:
generate_private_key_failed:
  g_free(private_key);
  signed_video_free(priv->signed_video);
  priv->signed_video = NULL;
create_failed:
unsupported_codec:
  return FALSE;
}

static gboolean
gst_signing_sink_event(GstBaseTransform *trans, GstEvent *event)
{
  GstSigning *signing = GST_SIGNING(trans);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_EOS:
      push_access_unit_at_eos(signing);
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS(gst_signing_parent_class)->sink_event(trans, event);
}
