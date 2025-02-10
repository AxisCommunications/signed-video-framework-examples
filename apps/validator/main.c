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
 * This application validates the authenticity of a video from a file. The result is written on
 * screen and in addition, a summary is written to the file validation_results.txt.
 *
 * Supported video codecs are H26x and the recording should be either an .mp4, or a .mkv file. Other
 * formats may also work, but have not been tested.
 *
 * Example to validate the authenticity of an h264 video stored in file.mp4
 *   $ ./validator.exe -c h264 /path/to/file.mp4
 */

#include <glib.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <stdio.h>  // FILE, fopen, fclose
#include <string.h>  // strcpy, strcat, strcmp, strlen
#include <time.h>  // time_t, struct tm, strftime, gmtime

#include <signed-video-framework/signed_video_auth.h>
#include <signed-video-framework/signed_video_common.h>

#define RESULTS_FILE "validation_results.txt"
// Increment VALIDATOR_VERSION when a change is affecting the code.
#define VALIDATOR_VERSION "v2.0.0"  // Requires at least signed-video-framework v2.0.1

typedef struct {
  GMainLoop *loop;
  GstElement *source;
  GstElement *sink;

  signed_video_t *sv;
  signed_video_authenticity_t *auth_report;
  signed_video_product_info_t *product_info;
  char *version_on_signing_side;
  char *this_version;
  bool no_container;
  SignedVideoCodec codec;
  gsize total_bytes;
  gsize sei_bytes;

  gint valid_gops;
  gint valid_gops_with_missing;
  gint invalid_gops;
  gint no_sign_gops;
} ValidationData;

#define STR_PREFACE_SIZE 11  // Largest possible size including " : "
#define VALIDATION_VALID    "valid    : "
#define VALIDATION_INVALID  "invalid  : "
#define VALIDATION_UNSIGNED "unsigned : "
#define VALIDATION_SIGNED   "signed   : "
#define VALIDATION_MISSING  "missing  : "
#define VALIDATION_ERROR    "error    : "
#define NALU_TYPES_PREFACE  "   nalus : "

#define VALIDATION_STRUCTURE_NAME "validation-result"
#define VALIDATION_FIELD_NAME "result"

/* Need to be the same as in signed-video-framework. */
static const uint8_t kUuidSignedVideo[16] = {
    0x53, 0x69, 0x67, 0x6e, 0x65, 0x64, 0x20, 0x56, 0x69, 0x64, 0x65, 0x6f, 0x2e, 0x2e, 0x2e, 0x30};

/* AV1 */
/* Helpers when parsing OBUs if av1parse cannot be used. */
static guint8 *ongoing_obu = NULL;
static gsize ongoing_obu_tot_size = 0;
static gsize ongoing_obu_size = 0;
/* If set to 'false', will use av1parse, which currently cannot parse OBU Metadata of type
 * user private. */
const bool parse_av1_manually = true;
#define METADATA_TYPE_USER_PRIVATE 25

/* Helper function that copies a string and (re)allocates memory if necessary. */
static gint
reallocate_memory_and_copy_string(gchar **dst_str, const gchar *src_str)
{
  if (!dst_str) return 0;
  // If the |src_str| is a NULL pointer make sure to copy an empty string.
  if (!src_str) src_str = "";

  gsize dst_size = *dst_str ? strlen(*dst_str) + 1 : 0;
  const gsize src_size = strlen(src_str) + 1;
  gint success = 0;
  if (src_size != dst_size) {
    gchar *new_dst_str = realloc(*dst_str, src_size);
    if (!new_dst_str) goto done;

    *dst_str = new_dst_str;
  }
  strcpy(*dst_str, src_str);
  success = 1;

done:
  if (!success) {
    free(*dst_str);
    *dst_str = NULL;
  }

  return success;
}

/* Helper function to copy signed_video_product_info_t. */
static gint
copy_product_info(signed_video_product_info_t *dst, const signed_video_product_info_t *src)
{
  if (!src) return 0;

  gint success = 0;
  if (!reallocate_memory_and_copy_string(&dst->hardware_id, src->hardware_id)) goto done;
  if (!reallocate_memory_and_copy_string(&dst->firmware_version, src->firmware_version)) goto done;
  if (!reallocate_memory_and_copy_string(&dst->serial_number, src->serial_number)) goto done;
  if (!reallocate_memory_and_copy_string(&dst->manufacturer, src->manufacturer)) goto done;
  if (!reallocate_memory_and_copy_string(&dst->address, src->address)) goto done;

  success = 1;

done:

  return success;
}

static void
post_validation_result_message(GstAppSink *sink, GstBus *bus, const gchar *result)
{
  GstStructure *structure = gst_structure_new(
      VALIDATION_STRUCTURE_NAME, VALIDATION_FIELD_NAME, G_TYPE_STRING, result, NULL);

  if (!gst_bus_post(bus, gst_message_new_element(GST_OBJECT(sink), structure))) {
    g_error("failed to post validation results message");
  }
}

/* Checks if the |nalu| is a SEI/OBU Metadata generated by Signed Video. */
static bool
is_signed_video_sei(const guint8 *nalu, SignedVideoCodec codec)
{
  int num_zeros = 0;
  int idx = 0;
  bool is_sei_user_data_unregistered = false;

  if (codec == SV_CODEC_AV1) {
    // Determine if OBU is of type metadata
    is_sei_user_data_unregistered = ((nalu[idx] & 0x78) >> 3 == 5);
    idx++;
    if (!is_sei_user_data_unregistered) return false;

    // Move past payload size
    int shift_bits = 0;
    int payload_size = 0;
    // Get payload size (including uuid).
    while (true) {
      int byte = nalu[idx] & 0xff;
      payload_size |= (byte & 0x7F) << shift_bits;
      idx++;
      if ((byte & 0x80) == 0)
        break;
      shift_bits += 7;
    }
    if (payload_size < 20) return false;

    // Determine if this is an OBU Metadata of type user private (25).
    is_sei_user_data_unregistered = (nalu[idx] == METADATA_TYPE_USER_PRIVATE);
    idx++;
    if (!is_sei_user_data_unregistered) return false;

    // Move past intermediate trailing byte
    idx++;
  } else {
    // Check first (at most) 4 bytes for a start code.
    while (nalu[idx] == 0 && idx < 4) {
      num_zeros++;
      idx++;
    }
    if (num_zeros == 4) {
      // This is simply wrong.
      return false;
    } else if ((num_zeros == 3 || num_zeros == 2) && (nalu[idx] == 1)) {
      // Start code present. Move to next byte.
      idx++;
    } else {
      // Start code NOT present. Assume the first 4 bytes have been replaced with size,
      // which is common in, e.g., gStreamer.
      idx = 4;
    }

    // Determine if this is a SEI of type user data unregistered.
    if (codec == SV_CODEC_H264) {
      // H.264: 0x06 0x05
      is_sei_user_data_unregistered = (nalu[idx] == 6) && (nalu[idx + 1] == 5);
      idx += 2;
    } else if (codec == SV_CODEC_H265) {
      // H.265: 0x4e 0x?? 0x05
      is_sei_user_data_unregistered = ((nalu[idx] & 0x7e) >> 1 == 39) && (nalu[idx + 2] == 5);
      idx += 3;
    }
    if (!is_sei_user_data_unregistered) return false;

    // Move past payload size
    while (nalu[idx] == 0xff) {
      idx++;
    }
    idx++;
  }

  // Verify Signed Video UUID (16 bytes).
  return memcmp(&nalu[idx], kUuidSignedVideo, 16) == 0 ? true : false;
}

static gsize
av1_get_next_obu(const guint8 *data)
{
  const guint8* next_obu = data;
  gsize obu_size = 0;
  next_obu++; // Move past OBU header

  int shift = 0;
  int obu_length = 0;
  // OBU length leb128()
  while (true) {
    int byte = *next_obu & 0xff;
    obu_length |= (byte & 0x7f) << shift;
    next_obu++;
    if ((byte & 0x80) == 0)
      break;
    shift += 7;
  }
  next_obu += obu_length;
  obu_size = (gsize)(next_obu - data);

  return obu_size;
}

static GstBuffer *
parse_av1(const guint8 *data, gsize data_size, gsize *slack_size, bool *more_to_come)
{
  const guint8* next_obu = data;
  gsize remaining_size = data_size;
  uint memories_left = gst_buffer_get_max_memory();

  GstBuffer *obu_buffer = gst_buffer_new();
  if (!obu_buffer) return NULL;

  *slack_size = 0;
  *more_to_come = false;
  while (remaining_size > 0 && memories_left > 0) {
    gsize obu_size = av1_get_next_obu(next_obu);
    if (obu_size > remaining_size) {
      *slack_size = remaining_size;
      remaining_size = 0;
    } else {
      gpointer *obu = g_malloc0(obu_size);
      memcpy(obu, next_obu, obu_size);
      GstMemory *memory = gst_memory_new_wrapped(0, obu, obu_size, 0, obu_size, obu, g_free);
      gst_buffer_append_memory(obu_buffer, memory);
      remaining_size -= obu_size;
      next_obu += obu_size;
      memories_left--;
    }
  }
  if (*slack_size == 0 && memories_left == 0) {
    *more_to_come = true;
    *slack_size = remaining_size;
  }

  return obu_buffer;
}

/* Called when the appsink notifies us that there is a new buffer ready for processing. */
static GstFlowReturn
on_new_sample_from_sink(GstElement *elt, ValidationData *data)
{
  g_assert(elt != NULL);

  GstAppSink *sink = GST_APP_SINK(elt);
  GstSample *sample = NULL;
  GstBuffer *sample_buffer = NULL;
  GstBuffer *buffer = NULL;
  GstBuffer *obu_buffer = NULL;
  GstBus *bus = NULL;
  GstMapInfo info;
  SignedVideoReturnCode status = SV_UNKNOWN_FAILURE;
  bool run_more = false;

  // Get the sample from appsink.
  sample = gst_app_sink_pull_sample(sink);
  // If sample is NULL the appsink is stopped or EOS is reached. Both are valid, hence proceed.
  if (sample == NULL) return GST_FLOW_OK;

  sample_buffer = gst_sample_get_buffer(sample);

  if ((sample_buffer == NULL) || (gst_buffer_n_memory(sample_buffer) == 0)) {
    g_debug("no buffer, or no memories in buffer");
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  if (data->codec == SV_CODEC_AV1 && parse_av1_manually) {
    GstMemory *mem = gst_buffer_peek_memory(sample_buffer, 0);
    if (!gst_memory_map(mem, &info, GST_MAP_READ)) {
      g_debug("failed to map memory");
      gst_sample_unref(sample);
      return GST_FLOW_ERROR;
    }
    if (ongoing_obu_tot_size < ongoing_obu_size + info.size) {
      guint8 *tmp = g_malloc0(ongoing_obu_size + info.size);
      memcpy(tmp, ongoing_obu, ongoing_obu_size);
      free(ongoing_obu);
      ongoing_obu = tmp;
    }
    memcpy(ongoing_obu + ongoing_obu_size, info.data, info.size);
    ongoing_obu_tot_size = ongoing_obu_size + info.size;
    ongoing_obu_size += info.size;
  }

try_again:
  if (data->codec == SV_CODEC_AV1 && parse_av1_manually) {
    gsize slack_size = 0;
    obu_buffer = parse_av1(ongoing_obu, ongoing_obu_size, &slack_size, &run_more);
    // Store slack data
    memcpy(ongoing_obu, ongoing_obu + ongoing_obu_size - slack_size, slack_size);
    memset(ongoing_obu + slack_size, 0, ongoing_obu_size - slack_size);
    ongoing_obu_size = slack_size;
    // Use OBU Buffer
    buffer = obu_buffer;
  } else {
    buffer = sample_buffer;
  }

  bus = gst_element_get_bus(elt);
  for (guint i = 0; i < gst_buffer_n_memory(buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, i);
    if (!gst_memory_map(mem, &info, GST_MAP_READ)) {
      g_debug("failed to map memory");
      gst_object_unref(bus);
      gst_sample_unref(sample);
      return GST_FLOW_ERROR;
    }

    // Update the total video and SEI sizes.
    data->total_bytes += info.size;
    data->sei_bytes += is_signed_video_sei(info.data, data->codec) ? info.size : 0;

    if (data->no_container || data->codec == SV_CODEC_AV1) {
      status = signed_video_add_nalu_and_authenticate(
          data->sv, info.data, info.size, &(data->auth_report));
    } else {
      // Pass nalu to the signed video session, excluding 4 bytes start code, since it might have
      // been replaced by the size of buffer.
      // TODO: First, a check for 3 or 4 byte start code should be done.
      status = signed_video_add_nalu_and_authenticate(
          data->sv, info.data + 4, info.size - 4, &(data->auth_report));
    }
    if (status != SV_OK) {
      g_critical("error during verification of signed video: %d", status);
      post_validation_result_message(sink, bus, VALIDATION_ERROR);
    } else if (data->auth_report) {
      gsize str_size = 1;  // starting with a new-line character to align strings
      str_size += STR_PREFACE_SIZE;
      str_size += strlen(data->auth_report->latest_validation.validation_str);
      str_size += 1;  // new-line character
      str_size += STR_PREFACE_SIZE;
      str_size += strlen(data->auth_report->latest_validation.nalu_str);
      str_size += 1;  // null-terminated
      gchar *result = g_malloc0(str_size);
      strcpy(result, "\n");
      strcat(result, NALU_TYPES_PREFACE);
      strcat(result, data->auth_report->latest_validation.nalu_str);
      strcat(result, "\n");
      switch (data->auth_report->latest_validation.authenticity) {
        case SV_AUTH_RESULT_OK:
          data->valid_gops++;
          strcat(result, VALIDATION_VALID);
          break;
        case SV_AUTH_RESULT_NOT_OK:
          data->invalid_gops++;
          strcat(result, VALIDATION_INVALID);
          break;
        case SV_AUTH_RESULT_OK_WITH_MISSING_INFO:
          data->valid_gops_with_missing++;
          g_debug("gops with missing info since last verification");
          strcat(result, VALIDATION_MISSING);
          break;
        case SV_AUTH_RESULT_NOT_SIGNED:
          data->no_sign_gops++;
          g_debug("gop is not signed");
          strcat(result, VALIDATION_UNSIGNED);
          break;
        case SV_AUTH_RESULT_SIGNATURE_PRESENT:
          g_debug("gop is signed, but not yet validated");
          strcat(result, VALIDATION_SIGNED);
          break;
        default:
          break;
      }
      strcat(result, data->auth_report->latest_validation.validation_str);
      post_validation_result_message(sink, bus, result);
      // Allocate memory for |product_info| the first time it will be copied from the authenticity
      // report.
      if (!data->product_info) {
        data->product_info =
            (signed_video_product_info_t *)g_malloc0(sizeof(signed_video_product_info_t));
      }
      if (!copy_product_info(data->product_info, &(data->auth_report->product_info))) {
        g_warning("product info could not be transfered from authenticity report");
      }
      // Allocate memory and copy version strings.
      if (strlen(data->auth_report->this_version) > 0) {
        if (strcmp(data->this_version, data->auth_report->this_version) != 0) {
          g_error("unexpected mismatch in 'this_version'");
        }
      }
      if (!data->version_on_signing_side &&
          (strlen(data->auth_report->version_on_signing_side) > 0)) {
        data->version_on_signing_side =
            g_malloc0(strlen(data->auth_report->version_on_signing_side) + 1);
        if (!data->version_on_signing_side) {
          g_warning("failed allocating memory for version_on_signing_side");
        } else {
          strcpy(data->version_on_signing_side, data->auth_report->version_on_signing_side);
        }
      }
      signed_video_authenticity_report_free(data->auth_report);
      g_free(result);
    }
    gst_memory_unmap(mem, &info);
  }
  if (obu_buffer) gst_buffer_unref(obu_buffer);
  obu_buffer = NULL;
  if (run_more) {
    goto try_again;
  }

  gst_object_unref(bus);
  if (!(data->codec == SV_CODEC_AV1 && parse_av1_manually)) {
    // If data is passed in from a file the ownership is not transferred until end of file
    gst_sample_unref(sample);
  }

  return GST_FLOW_OK;
}

/* Called when a GstMessage is received from the source pipeline. */
static gboolean
on_source_message(GstBus __attribute__((unused)) *bus, GstMessage *message, ValidationData *data)
{
  FILE *f = NULL;
  char *this_version = data->this_version;
  char *signing_version = data->version_on_signing_side;
  char first_ts_str[80] = {'\0'};
  char last_ts_str[80] = {'\0'};
  bool has_timestamp = false;
  float bitrate_increase = 0.0f;
  bool is_unsigned = false;

  if (data->total_bytes) {
    bitrate_increase = 100.0f * data->sei_bytes / (float)(data->total_bytes - data->sei_bytes);
  }

  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
      data->auth_report = signed_video_get_authenticity_report(data->sv);
      if (data->auth_report && data->auth_report->accumulated_validation.has_timestamp) {
        time_t first_sec = data->auth_report->accumulated_validation.first_timestamp / 1000000;
        struct tm first_ts = *gmtime(&first_sec);
        strftime(first_ts_str, sizeof(first_ts_str), "%a %Y-%m-%d %H:%M:%S %Z", &first_ts);
        time_t last_sec = data->auth_report->accumulated_validation.last_timestamp / 1000000;
        struct tm last_ts = *gmtime(&last_sec);
        strftime(last_ts_str, sizeof(last_ts_str), "%a %Y-%m-%d %H:%M:%S %Z", &last_ts);
        has_timestamp = true;
      }
      signed_video_authenticity_report_free(data->auth_report);
      g_debug("received EOS");
      f = fopen(RESULTS_FILE, "w");
      if (!f) {
        g_warning("Could not open %s for writing", RESULTS_FILE);
        g_main_loop_quit(data->loop);
        return FALSE;
      }
      fprintf(f, "-----------------------------\n");
      if (data->auth_report) {
        SignedVideoPublicKeyValidation public_key_validation =
            data->auth_report->accumulated_validation.public_key_validation;
        if (public_key_validation == SV_PUBKEY_VALIDATION_OK) {
          fprintf(f, "PUBLIC KEY IS VALID!\n");
        } else if (public_key_validation == SV_PUBKEY_VALIDATION_NOT_OK) {
          fprintf(f, "PUBLIC KEY IS NOT VALID!\n");
        } else {
          fprintf(f, "PUBLIC KEY COULD NOT BE VALIDATED!\n");
        }
      } else {
        fprintf(f, "PUBLIC KEY COULD NOT BE VALIDATED!\n");
      }
      fprintf(f, "-----------------------------\n");
      if (data->invalid_gops > 0) {
        fprintf(f, "VIDEO IS INVALID!\n");
      } else if (data->valid_gops_with_missing > 0) {
        fprintf(f, "VIDEO IS VALID, BUT HAS MISSING FRAMES!\n");
      } else if (data->valid_gops > 0) {
        fprintf(f, "VIDEO IS VALID!\n");
      } else if (data->no_sign_gops > 0) {
        fprintf(f, "VIDEO IS NOT SIGNED!\n");
      } else if (data->auth_report) {
        fprintf(f, "VIDEO IS NOT SIGNED!\n");
        is_unsigned = true;
      } else {
        fprintf(f, "NO COMPLETE GOPS FOUND!\n");
      }
      gint num_unsigned_gops = (data->invalid_gops || data->valid_gops_with_missing || data->valid_gops) ? 0 : data->no_sign_gops;
      if (is_unsigned) {
        fprintf(f, "Number of unsigned Bitstream Units: %u\n", data->auth_report->accumulated_validation.number_of_received_nalus);
      } else {
        fprintf(f, "Number of valid GOPs: %d\n", data->valid_gops);
        fprintf(f, "Number of valid GOPs with missing BUs: %d\n", data->valid_gops_with_missing);
        fprintf(f, "Number of invalid GOPs: %d\n", data->invalid_gops);
        fprintf(f, "Number of GOPs without signature: %d\n", num_unsigned_gops);
      }
      fprintf(f, "-----------------------------\n");
      fprintf(f, "\nProduct Info\n");
      fprintf(f, "-----------------------------\n");
      if (data->product_info) {
        fprintf(f, "Hardware ID:      %s\n", data->product_info->hardware_id);
        fprintf(f, "Serial Number:    %s\n", data->product_info->serial_number);
        fprintf(f, "Firmware version: %s\n", data->product_info->firmware_version);
        fprintf(f, "Manufacturer:     %s\n", data->product_info->manufacturer);
        fprintf(f, "Address:          %s\n", data->product_info->address);
      } else {
        fprintf(f, "NOT PRESENT!\n");
      }
      fprintf(f, "-----------------------------\n");
      fprintf(f, "\nSigned Video timestamps\n");
      fprintf(f, "-----------------------------\n");
      fprintf(f, "First frame:           %s\n", has_timestamp ? first_ts_str : "N/A");
      fprintf(f, "Last validated frame:  %s\n", has_timestamp ? last_ts_str : "N/A");
      fprintf(f, "-----------------------------\n");
      fprintf(f, "\nSigned Video size footprint\n");
      fprintf(f, "-----------------------------\n");
      fprintf(f, "Total video:       %8zu B\n", data->total_bytes);
      fprintf(f, "Signed Video data: %8zu B\n", data->sei_bytes);
      fprintf(f, "Bitrate increase: %9.2f %%\n", bitrate_increase);
      fprintf(f, "-----------------------------\n");
      fprintf(f, "\nVersions of signed-video-framework\n");
      fprintf(f, "-----------------------------\n");
      fprintf(f, "Validator (%s) runs: %s\n", VALIDATOR_VERSION, this_version);
      fprintf(f, "Camera runs:             %s\n", signing_version ? signing_version : "N/A");
      fprintf(f, "-----------------------------\n");
      fclose(f);
      g_message("Validation performed with Signed Video version %s", this_version);
      if (signing_version) {
        g_message("Signing was performed with Signed Video version %s", signing_version);
      }
      g_message("Validation complete. Results printed to '%s'.", RESULTS_FILE);
      g_main_loop_quit(data->loop);
      break;
    case GST_MESSAGE_ERROR:
      g_debug("received error");
      g_main_loop_quit(data->loop);
      break;
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s = gst_message_get_structure(message);
      if (strcmp(gst_structure_get_name(s), VALIDATION_STRUCTURE_NAME) == 0) {
        const gchar *result = gst_structure_get_string(s, VALIDATION_FIELD_NAME);
        g_message("Latest authenticity result:\t%s", result);
      }
    } break;
    default:
      break;
  }
  return TRUE;
}

int
main(int argc, char **argv)
{
  int status = 1;
  GError *error = NULL;
  GstElement *validatorsink = NULL;
  GstBus *bus = NULL;
  ValidationData *data = NULL;
  SignedVideoCodec codec = -1;

  int arg = 1;
  gchar *format_str = "byte-stream,alignment=(string)nal";
  gchar *codec_str = "h264";
  gchar *demux_str = "";  // No container by default
  gchar *filename = NULL;
  gchar *pipeline = NULL;
  gchar *usage = g_strdup_printf(
      "Usage:\n%s [-h] [-c codec] filename\n\n"
      "Optional\n"
      "  -c codec  : 'h264' (default), 'h265' or 'av1'\n"
      "Required\n"
      "  filename  : Name of the file to be validated.\n",
      argv[0]);

  // Initialization.
  if (!gst_init_check(NULL, NULL, &error)) {
    g_warning("gst_init failed: %s", error->message);
    goto out;
  }

  // Parse options from command-line.
  while (arg < argc) {
    if (strcmp(argv[arg], "-h") == 0) {
      g_message("\n%s\n", usage);
      status = 0;
      goto out;
    } else if (strcmp(argv[arg], "-c") == 0) {
      arg++;
      codec_str = argv[arg];
    } else if (strncmp(argv[arg], "-", 1) == 0) {
      // Unknown option.
      g_message("Unknown option: %s\n%s", argv[arg], usage);
    } else {
      // End of options.
      break;
    }
    arg++;
  }

  // Parse filename.
  if (arg < argc) filename = argv[arg];
  if (!filename ) {
    g_warning("no filename was specified\n%s", usage);
    goto out;
  }
  g_free(usage);
  usage = NULL;

  // Determine if file is a container
  if (strstr(filename, ".mkv")) {
    // Matroska container (.mkv)
    demux_str = "! matroskademux";
  } else if (strstr(filename, ".mp4")) {
    // MP4 container (.mp4)
    demux_str = "! qtdemux";
  }

  // Set codec.
  if (strcmp(codec_str, "h264") == 0 || strcmp(codec_str, "h265") == 0) {
    codec = (strcmp(codec_str, "h264") == 0) ? SV_CODEC_H264 : SV_CODEC_H265;
  } else if (strcmp(codec_str, "av1") == 0) {
    codec = SV_CODEC_AV1;
    format_str = "obu-stream,alignment=(string)obu";
  } else {
    g_warning("unsupported codec format '%s'", codec_str);
    goto out;
  }

  if (parse_av1_manually) {
    if (g_file_test(filename, G_FILE_TEST_EXISTS) && (codec != SV_CODEC_AV1)) {
      pipeline = g_strdup_printf(
          "filesrc location=\"%s\" %s ! %sparse ! "
          "video/x-%s,stream-format=byte-stream,alignment=(string)nal ! appsink "
          "name=validatorsink",
          filename, demux_str, codec_str, codec_str);
    } else if (g_file_test(filename, G_FILE_TEST_EXISTS) && (codec == SV_CODEC_AV1)) {
      pipeline = g_strdup_printf("filesrc location=\"%s\" %s ! appsink name=validatorsink", filename, demux_str);
    } else {
      g_warning("file '%s' does not exist", filename);
      goto out;
    }
  } else {
    if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
      pipeline = g_strdup_printf(
          "filesrc location=\"%s\" %s ! %sparse ! "
          "video/x-%s,stream-format=%s ! appsink "
          "name=validatorsink",
          filename, demux_str, codec_str, codec_str, format_str);
    } else {
      g_warning("file '%s' does not exist", filename);
      goto out;
    }
  }
  g_message("GST pipeline: %s", pipeline);

  data = g_new0(ValidationData, 1);
  // Initialize data.
  data->valid_gops = 0;
  data->valid_gops_with_missing = 0;
  data->invalid_gops = 0;
  data->no_sign_gops = 0;
  data->sv = signed_video_create(codec);
  data->loop = g_main_loop_new(NULL, FALSE);
  data->source = gst_parse_launch(pipeline, NULL);
  data->no_container = (strlen(demux_str) == 0);
  data->codec = codec;
  data->this_version = g_malloc0(strlen(signed_video_get_version()) + 1);
  strcpy(data->this_version, signed_video_get_version());

  g_free(pipeline);
  pipeline = NULL;

  if (data->source == NULL || data->loop == NULL || data->sv == NULL) {
    g_warning(
        "init failed: source = (%p), loop = (%p), sv = (%p)", data->source, data->loop, data->sv);
    goto out;
  }
  // To be notified of messages from this pipeline; error, EOS and live validation.
  bus = gst_element_get_bus(data->source);
  gst_bus_add_watch(bus, (GstBusFunc)on_source_message, data);

  // Use appsink in push mode. It sends a signal when data is available and pulls out the data in
  // the signal callback. Set the appsink to push as fast as possible, hence set sync=false.
  validatorsink = gst_bin_get_by_name(GST_BIN(data->source), "validatorsink");
  g_object_set(G_OBJECT(validatorsink), "emit-signals", TRUE, "sync", FALSE, NULL);
  g_signal_connect(validatorsink, "new-sample", G_CALLBACK(on_new_sample_from_sink), data);
  gst_object_unref(validatorsink);

  // Launching things.
  if (gst_element_set_state(data->source, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    // Check if there is an error message with details on the bus.
    GstMessage *msg = gst_bus_poll(bus, GST_MESSAGE_ERROR, 0);
    if (msg) {
      gst_message_parse_error(msg, &error, NULL);
      g_printerr("Failed to start up source: %s", error->message);
      gst_message_unref(msg);
    } else {
      g_error("Failed to start up source!");
    }
    goto out;
  }

  // Let's run!
  // This loop will quit when the sink pipeline goes EOS or when an error occurs in sink pipelines.
  g_main_loop_run(data->loop);

  gst_element_set_state(data->source, GST_STATE_NULL);

  status = 0;
out:
  // End of session. Free objects.
  if (bus) gst_object_unref(bus);
  g_free(usage);
  g_free(pipeline);
  if (error) g_error_free(error);
  if (data) {
    if (data->source) gst_object_unref(data->source);
    g_main_loop_unref(data->loop);
    signed_video_free(data->sv);
    g_free(data->product_info);
    g_free(data->this_version);
    g_free(data->version_on_signing_side);
    g_free(data);
  }

  return status;
}
