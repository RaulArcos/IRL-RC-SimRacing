#include <gst/gst.h>
#include <glib.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static GMainLoop* g_loop = nullptr;

static void onSignal(int)
{
    if (g_loop) g_main_loop_quit(g_loop);
}

static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer /*data*/)
{
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        std::fprintf(stderr, "[GST] ERROR: %s\n", err ? err->message : "unknown");
        if (dbg) std::fprintf(stderr, "[GST] DEBUG: %s\n", dbg);
        if (err) g_error_free(err);
        if (dbg) g_free(dbg);
        if (g_loop) g_main_loop_quit(g_loop);
        break;
    }
    case GST_MESSAGE_EOS:
        std::printf("[GST] EOS\n");
        if (g_loop) g_main_loop_quit(g_loop);
        break;
    default:
        break;
    }
    (void)bus;
    return TRUE;
}

static void set_caps(GstElement* capsfilter, int width, int height, int fps)
{
    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, fps, 1,
        nullptr);

    g_object_set(G_OBJECT(capsfilter), "caps", caps, nullptr);
    gst_caps_unref(caps);
}

int main(int argc, char** argv)
{
    const char* host = (argc >= 2) ? argv[1] : "192.168.0.188";
    const int port = (argc >= 3) ? std::atoi(argv[2]) : 5600;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    gst_init(&argc, &argv);

    GstElement* pipeline   = gst_pipeline_new("video_tx");
    GstElement* src        = gst_element_factory_make("libcamerasrc", "src");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "caps");
    GstElement* queue      = gst_element_factory_make("queue", "q");
    GstElement* enc        = gst_element_factory_make("v4l2h264enc", "enc");
    GstElement* queue2     = gst_element_factory_make("queue", "q2");
    GstElement* parse      = gst_element_factory_make("h264parse", "parse");
    GstElement* mux        = gst_element_factory_make("mpegtsmux", "mux");
    GstElement* sink       = gst_element_factory_make("udpsink", "sink");

    if (!pipeline || !src || !capsfilter || !queue || !enc || !queue2 || !parse || !mux || !sink) {
        std::fprintf(stderr, "Failed to create one or more GStreamer elements.\n");
        std::fprintf(stderr, "Check plugins installed: libcamerasrc, v4l2h264enc, h264parse, mpegtsmux.\n");
        return 1;
    }

    set_caps(capsfilter, 640, 360, 30);

    g_object_set(G_OBJECT(queue),
                 "max-size-buffers", 1,
                 "max-size-bytes", 0,
                 "max-size-time", (guint64)0,
                 "leaky", 2,
                 nullptr);

    g_object_set(G_OBJECT(enc),
                 "extra-controls",
                 "controls,video_bitrate=1200000,h264_i_frame_period=15",
                 nullptr);

    g_object_set(G_OBJECT(queue2),
                 "max-size-buffers", 2,
                 "max-size-bytes", 0,
                 "max-size-time", (guint64)0,
                 "leaky", 2,
                 nullptr);

    g_object_set(G_OBJECT(parse),
                 "config-interval", 1,
                 nullptr);

    g_object_set(G_OBJECT(mux),
                 "alignment", 7,
                 nullptr);

    g_object_set(G_OBJECT(sink),
                 "host", host,
                 "port", port,
                 "sync", FALSE,
                 "async", FALSE,
                 nullptr);

    gst_bin_add_many(GST_BIN(pipeline), src, capsfilter, queue, enc, queue2, parse, mux, sink, nullptr);

    if (!gst_element_link_many(src, capsfilter, queue, enc, queue2, parse, mux, sink, nullptr)) {
        std::fprintf(stderr, "Failed to link pipeline elements\n");
        return 1;
    }

    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, nullptr);
    gst_object_unref(bus);

    std::printf("Starting video TX to %s:%d (640x360@30 H264/MPEGTS over UDP)\n", host, port);

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::fprintf(stderr, "Failed to set pipeline to PLAYING\n");
        gst_object_unref(pipeline);
        return 1;
    }

    g_loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(g_loop);

    std::printf("\nStopping...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(pipeline);
    if (g_loop) {
        g_main_loop_unref(g_loop);
        g_loop = nullptr;
    }

    return 0;
}
