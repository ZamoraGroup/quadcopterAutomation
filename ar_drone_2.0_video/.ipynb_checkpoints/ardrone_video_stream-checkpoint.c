/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <gstnvdsmeta.h>

#include <gst/rtsp-server/rtsp-server.h>
#define MAX_DISPLAY_LEN 64

#define PGIE_CLASS_ID_VEHICLE 0
#define PGIE_CLASS_ID_PERSON 2

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 640
#define MUXER_OUTPUT_HEIGHT 360

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 4000000

gint frame_number = 0;
gchar pgie_classes_str[4][32] = { "Vehicle", "TwoWheeler", "Person", "Roadsign"};

/* Functions below print the Capabilities in human-friendly form*/
static gboolean print_field(GQuark field, const GValue *value, gpointer pfx){
	gchar *str = gst_value_serialize(value);

	g_print("%s %15s: %s\n", (gchar *) pfx, g_quark_to_string(field), str);
	g_free(str);
	return TRUE;
}

static void print_caps(const GstCaps *caps, const gchar * pfx)
{
	guint i;

	g_return_if_fail(caps != NULL);

	if(gst_caps_is_any(caps)){
		g_print("%sANY\n", pfx);
		return;
	}
	if(gst_caps_is_empty(caps)){
		g_print("%sEMPTY\n", pfx);
		return;
	}

	for(i = 0; i < gst_caps_get_size(caps); i++)
	{
		GstStructure *structure = gst_caps_get_structure(caps, i);

		g_print("%s%s\n", pfx, gst_structure_get_name(structure));
		gst_structure_foreach(structure, print_field, (gpointer)pfx);
	}
}

/* Prints information about a Pad Template, including its Capabilities */
static void print_pad_templates_information(GstElementFactory *factory)
{
	const GList *pads;
	GstStaticPadTemplate *padtemplate;

	g_print("Pad Templates for %s:\n", gst_element_factory_get_longname(factory));
	if(!gst_element_factory_get_num_pad_templates(factory))
	{
		g_print(" none\n");
		return;
	}

	pads = gst_element_factory_get_static_pad_templates(factory);
	while(pads)
	{
		padtemplate = pads->data;
		pads = g_list_next(pads);

		if(padtemplate->direction == GST_PAD_SRC)
		g_print("SRC template: '%s' \n", padtemplate->name_template);
		else if(padtemplate->direction == GST_PAD_SINK)
			g_print("   SINK template: '%s'\n", padtemplate->name_template);
		else
			g_print("   UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

		if(padtemplate->presence == GST_PAD_ALWAYS)
			g_print("    Availability: Always\n");
		else if(padtemplate->presence == GST_PAD_SOMETIMES)
			g_print("    Availability: Sometimes\n");
		else if(padtemplate->presence == GST_PAD_REQUEST)
			g_print("    Availability: On request\n");
		else 
			g_print("    Availability: UNKNOWN!!!\n");

		if(padtemplate->static_caps.string)
		{
			GstCaps *caps;
			g_print("    Capabilities:\n");
			caps = gst_static_caps_get(&padtemplate->static_caps);
			print_caps(caps, "    ");
			gst_caps_unref(caps);
		}

		g_print("\n");
	}
}

	/* Shows the CURRENT capabilities of the requested pad in the given element */
	static void print_pad_capabilities(GstElement *element, gchar *pad_name)
	{
		GstPad *pad = NULL;
		GstCaps *caps = NULL;

		/* Retrieve pad */
		pad = gst_element_get_static_pad(element, pad_name);
		if(!pad)
		{
			g_printerr("Could not retrieve pad '%s'\n", pad_name);
			return;
		}

		/* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
		caps = gst_pad_get_current_caps (pad);
		if(!caps)
			caps = gst_pad_query_caps(pad, NULL);

		/* Print and free */
		g_print("Caps for the %s pad:\n", pad_name);
		print_caps(caps, "     ");
		gst_caps_unref(caps);
		gst_object_unref(pad);
	}


/* osd_sink_pad_buffer_probe  will extract metadata received on OSD sink pad
 * and update params for drawing rectangle, object information etc. */
static GstPadProbeReturn osd_sink_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
	GstBuffer *buf = (GstBuffer *) info->data;
	guint num_rects = 0;
	NvDsObjectMeta *obj_meta = NULL;
	guint vehicle_count = 0;
	guint person_count = 0;
	NvDsMetaList * l_frame = NULL;
	NvDsMetaList * l_obj = NULL;
	NvDsDisplayMeta *display_meta = NULL;

	NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);

	for (l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next)
       	{
		NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);
		int offset = 0;
		for (l_obj = frame_meta->obj_meta_list; l_obj != NULL; l_obj = l_obj->next) 
		{
			obj_meta = (NvDsObjectMeta *) (l_obj->data);
			if (obj_meta->class_id == PGIE_CLASS_ID_VEHICLE) {
				vehicle_count++;
				num_rects++;
			}
			if (obj_meta->class_id == PGIE_CLASS_ID_PERSON) {
				person_count++;
				num_rects++;
			}
		}
		display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
		NvOSD_TextParams *txt_params  = &display_meta->text_params[0];
		display_meta->num_labels = 1;
		txt_params->display_text = g_malloc0 (MAX_DISPLAY_LEN);
		offset = snprintf(txt_params->display_text, MAX_DISPLAY_LEN, "Person = %d ", person_count);
		offset = snprintf(txt_params->display_text + offset , MAX_DISPLAY_LEN, "Vehicle = %d ", vehicle_count);

		/* Now set the offsets where the string should appear */
		txt_params->x_offset = 10;
		txt_params->y_offset = 12;

		/* Font , font-color and font-size */
		txt_params->font_params.font_name = "Courier";
		txt_params->font_params.font_size = 24;
		txt_params->font_params.font_color.red = 1.0;
		txt_params->font_params.font_color.green = 1.0;
		txt_params->font_params.font_color.blue = 1.0;
		txt_params->font_params.font_color.alpha = 1.0;

		/* Text background color */
		txt_params->set_bg_clr = 1;
		txt_params->text_bg_clr.red = 0.0;
		txt_params->text_bg_clr.green = 0.0;
		txt_params->text_bg_clr.blue = 0.0;
		txt_params->text_bg_clr.alpha = 1.0;

		nvds_add_display_meta_to_frame(frame_meta, display_meta);
	}

	g_print ("Frame Number = %d Number of objects = %d "
			"Vehicle Count = %d Person Count = %d\n",
			frame_number, num_rects, vehicle_count, person_count);
	frame_number++;
	return GST_PAD_PROBE_OK;
}

	static gboolean start_rtsp_streaming (guint rtsp_port_num, guint updsink_port_num)
{

	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;
	char udpsrc_pipeline[512];

	char port_num_Str[64] = { 0 };
	char *encoder_name;

	encoder_name = "H264";

	sprintf (udpsrc_pipeline,
			"( udpsrc name=pay0 port=%d caps=\"application/x-rtp, media=video, "
			"clock-rate=90000, encoding-name=%s, payload=96 \" )",
			updsink_port_num, encoder_name);

	sprintf (port_num_Str, "%d", rtsp_port_num);

	server = gst_rtsp_server_new ();
	g_object_set (server, "service", port_num_Str, NULL);

	mounts = gst_rtsp_server_get_mount_points (server);

	factory = gst_rtsp_media_factory_new ();
	gst_rtsp_media_factory_set_launch (factory, udpsrc_pipeline);

	gst_rtsp_mount_points_add_factory (mounts, "/ds-test", factory);

	g_object_unref (mounts);

	gst_rtsp_server_attach (server, NULL);

	g_print
		("\n *** DeepStream: Launched RTSP Streaming at rtsp://localhost:%d/ds-test ***\n\n",
		 rtsp_port_num);

	return TRUE;
}

static gboolean bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_EOS:
			g_print ("End of stream\n");
			g_main_loop_quit (loop);
			break;
		case GST_MESSAGE_ERROR:{
					       gchar *debug;
					       GError *error;
					       gst_message_parse_error (msg, &error, &debug);
					       g_printerr ("ERROR from element %s: %s\n",
							       GST_OBJECT_NAME (msg->src), error->message);
					       if (debug)
						       g_printerr ("Error details: %s\n", debug);
					       g_free (debug);
					       g_error_free (error);
					       g_main_loop_quit (loop);
					       break;
				       }
		default:
				       break;
	}
	return TRUE;
}

int main (int argc, char *argv[])
{
	GstElement *pipeline = NULL, *source = NULL, *h264parser = NULL, *decoder = NULL, *xvimagesink = NULL, // for drone video stream
		   *nvvidconv_src = NULL, *vidconv_src = NULL, *filter_src = NULL, *streammux = NULL, *pgie = NULL, *nvvidconv = NULL, 
		   *rgbaconverter = NULL, *nvosd = NULL, *encoder = NULL, *rtppay = NULL, *transform = NULL, *cap_filter = NULL, *sink = NULL;

	GstBus *bus = NULL;
	guint bus_watch_id;
	GstMessage *msg = NULL;
	GMainLoop *loop = NULL;
	GstCaps *caps = NULL, *caps_filter_src = NULL;
	GstPad *osd_sink_pad = NULL;
	/* Initialize GStreamer */
	gst_init (&argc, &argv);
	loop = g_main_loop_new (NULL, FALSE);
	/* Build the pipeline */
	pipeline = gst_pipeline_new("ardrone-pipeline");

	/* SOURCE ELEMENT FOR READING VIDEO STREAM */
	source = gst_element_factory_make("tcpclientsrc", "tcp-stream");

	/* define cap description */
	filter_src = gst_element_factory_make("capsfilter", "filter_src");
	caps_filter_src = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=640, height=360, framerate=15/1");
	g_object_set (G_OBJECT(filter_src), "caps", caps_filter_src, NULL);
	gst_caps_unref(caps_filter_src);

	/* Since the data format in the input file is elementary h264 stream,
	 * we need a h264parser */
	h264parser = gst_element_factory_make ("h264parse", "h264-parser");

	/* Use nvdec_h264 for hardware accelerated decode on GPU */
	decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");
	//decoder = gst_element_factory_make ("avdec_h264", "avdec-h264-decoder");

	/* Create nvstreammux instance to form batches from one or more sources. */
	streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

	if (!pipeline || !streammux) {
		g_printerr ("One element could not be created. Exiting.\n");
		return -1;
	}

	/* Use nvinfer to run inferencing on decoder's output,
	 * behaviour of inferencing is set through config file */
	pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");

	/* Use convertor to convert from NV12 to RGBA as required by nvosd */
	nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

	/* Create OSD to draw on the converted RGBA buffer */
	nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

	/* Finally render the osd output */
	transform = gst_element_factory_make ("nvvideoconvert", "transform");
	cap_filter = gst_element_factory_make ("capsfilter", "filter");
	caps = gst_caps_from_string ("video/x-raw(memory:NVMM), format=I420");
	g_object_set (G_OBJECT (cap_filter), "caps", caps, NULL);

	encoder = gst_element_factory_make ("nvv4l2h264enc", "h264-encoder");
	rtppay = gst_element_factory_make ("rtph264pay", "rtppay-h264");

	g_object_set (G_OBJECT (encoder), "bitrate", 4000000, NULL);

#ifdef PLATFORM_TEGRA
	g_object_set (G_OBJECT (encoder), "preset-level", 1, NULL);
	g_object_set (G_OBJECT (encoder), "insert-sps-pps", 1, NULL);
	g_object_set (G_OBJECT (encoder), "bufapi-version", 1, NULL);
#endif

	/* Video Display */
	xvimagesink = gst_element_factory_make("glimagesink", "sink");

	//sink = gst_element_factory_make ("nv3dsink", "sink");
	sink = gst_element_factory_make ("udpsink", "sink");
	g_object_set (G_OBJECT (sink), "host", "224.224.255.255", "port", 5400, "async", FALSE, "sync", 0, NULL);


	g_object_set(G_OBJECT(source), "host", "192.168.1.1", NULL); 
	g_object_set(G_OBJECT(source), "port", 5555, NULL); 

	if (!source) {
		g_printerr ("Source element could not be created. Exiting.\n");
		return -1;
	} 
	if (!h264parser) {
		g_printerr ("h264parser element could not be created. Exiting.\n");
		return -1;
	}
	if ( !decoder ) {
		g_printerr ("nvv4l2decoder element could not be created. Exiting.\n");
		//g_printerr ("avdec_h264 element could not be created. Exiting.\n");
		return -1;
	}
	if ( !xvimagesink) {
		g_printerr ("xvimagesink element could not be created. Exiting.\n");
		return -1;
	}
	if ( !sink) {
		g_printerr ("udpsink element could not be created. Exiting.\n");
		return -1;
	}

	/* we set the input filename to the source element */
	g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
			MUXER_OUTPUT_HEIGHT, "batch-size", 1,
			"batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC,
		       "live-source", "true", NULL);

	/* Set all the necessary properties of the nvinfer element,
	 * the necessary ones are : */
	g_object_set (G_OBJECT (pgie),
			"config-file-path", "dstest1_pgie_config.txt", NULL);

	print_pad_templates_information(gst_element_get_factory(source));
	print_pad_templates_information(gst_element_get_factory(filter_src));
	print_pad_templates_information(gst_element_get_factory(streammux));
	print_pad_templates_information(gst_element_get_factory(pgie));
	print_pad_templates_information(gst_element_get_factory(nvvidconv));
	print_pad_templates_information(gst_element_get_factory(nvosd));
	print_pad_templates_information(gst_element_get_factory(transform));
	print_pad_templates_information(gst_element_get_factory(cap_filter));
	print_pad_templates_information(gst_element_get_factory(encoder));
	print_pad_templates_information(gst_element_get_factory(sink));
	print_pad_templates_information(gst_element_get_factory(rtppay));


	/* we add a message handler */
	bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
	bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
	gst_object_unref (bus);

	/* Set the pipeline */
	/* add all the elements into the pipeline */
	gst_bin_add_many(GST_BIN(pipeline),
		       	source, 
			filter_src,
		       	//streammux,
		       	//pgie,
		       	nvvidconv, 
			//nvosd,
			transform,
		       	cap_filter,
		        encoder, 
			rtppay,
			sink, NULL);

	/* Print initial negotiated caps (is NULL state) */
      	g_print("In NULL state:\n");
	print_pad_capabilities(source, "source");
	print_pad_capabilities(filter_src, "filter");
	print_pad_capabilities(streammux, "streammux");
	print_pad_capabilities(pgie, "pgie");
	print_pad_capabilities(nvvidconv, "nvvidconv");
	print_pad_capabilities(nvosd, "nvosd");
	print_pad_capabilities(transform, "transform");
	print_pad_capabilities(cap_filter, "cap_filter");
	print_pad_capabilities(encoder, "encoder");
	print_pad_capabilities(rtppay, "rtppay");
      	print_pad_capabilities(sink, "sink");

	//GstPad *sinkpad, *srcpad;
	//gchar pad_name_sink[16] = "sink_0";
	//gchar pad_name_src[16] = "src";

	//sinkpad = gst_element_get_request_pad (streammux, pad_name_sink);
	//if (!sinkpad) {
	//	g_printerr ("Streammux request sink pad failed. Exiting.\n");
	//	return -1;
	//}

	//srcpad = gst_element_get_static_pad (filter_src, pad_name_src);
	//if (!srcpad) {
	//	g_printerr ("Decoder request src pad failed. Exiting.\n");
	//	return -1;
	//}

	//if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
	//	g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
	//	return -1;
	//}

	//gst_object_unref (sinkpad);
	//gst_object_unref (srcpad);


	/* we link the elements together */
	/* tcpclientsrc -> h264-parser -> avdec_h264 -> video-renderer */
//	if (!gst_element_link (source, filter_src)) {
//		g_printerr ("Elements could not be linked: 1. Exiting.\n");
//		return -1;
//	}
//
//	if (!gst_element_link_many (streammux, pgie,
//				nvosd, transform, cap_filter, encoder, 
//				rtppay,
//			       	sink,
//				NULL)) {
//		g_printerr ("Elements could not be linked: 2. Exiting.\n");
//		return -1;
//	}
  

	gboolean ret = TRUE;

	ret = start_rtsp_streaming (8554, 5400);
	if (ret != TRUE) {
		g_print ("%s: start_rtsp_straming function failed\n", __func__);
	}

	/* Lets add probe to get informed of the meta data generated, we add probe to
	 * the sink pad of the osd element, since by that time, the buffer would have
	 * had got all the metadata. */
//	osd_sink_pad = gst_element_get_static_pad (nvosd, "sink");
//	if (!osd_sink_pad)
//		g_print ("Unable to get sink pad\n");
//	else
//		gst_pad_add_probe (osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
//				osd_sink_pad_buffer_probe, NULL, NULL);
//
	/* Start playing */
	/* Set the pipeline to "playing" state */
	g_print ("Now playing: \n");
	gst_element_set_state (pipeline, GST_STATE_PLAYING);

	/* Wait until error or EOS */
	bus = gst_element_get_bus (pipeline);
	bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);

	msg =
		gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
				GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
	g_main_loop_run (loop);

	/* Free resources */
	if (msg != NULL)
		gst_message_unref (msg);
	g_print ("Returned, stopping playback\n");
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (bus);
	g_print ("Deleting pipeline\n");
	gst_object_unref (GST_OBJECT(pipeline));
	g_main_loop_unref (loop);
	return 0;
}
