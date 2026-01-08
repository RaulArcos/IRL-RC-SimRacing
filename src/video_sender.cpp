// video_tx.cpp
// Build:
//   g++ -std=c++17 -O2 -Wall -Wextra src/video_tx.cpp -o video_tx \
//     $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-base-1.0 glib-2.0)
//
// Run:
//   ./video_tx 192.168.0.188 5600

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

    // Create elements
    GstElement* pipeline   = gst_pipeline_new("video_tx");
    GstElement* src        = gst_element_factory_make("libcamerasrc", "src");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "caps");
    GstElement* queue      = gst_element_factory_make("queue", "q");
    GstElement* enc        = gst_element_factory_make("v4l2h264enc", "enc");
    GstElement* parse      = gst_element_factory_make("h264parse", "parse");
    GstElement* mux        = gst_element_factory_make("mpegtsmux", "mux");
    GstElement* sink       = gst_element_factory_make("udpsink", "sink");

    if (!pipeline || !src || !capsfilter || !queue || !enc || !parse || !mux || !sink) {
        std::fprintf(stderr, "Failed to create one or more GStreamer elements.\n");
        std::fprintf(stderr, "Check plugins installed: libcamerasrc, v4l2h264enc, h264parse, mpegtsmux.\n");
        return 1;
    }

    // Low-latency caps: start small
    set_caps(capsfilter, 640, 360, 60);

    // Queue: drop frames if downstream can't keep up
    g_object_set(G_OBJECT(queue),
                 "max-size-buffers", 2,
                 "leaky", 2, // 2 = downstream
                 nullptr);

    // Encoder tuning (properties vary by platform/driver)
    // extra-controls is supported by v4l2 elements; keep as string
    // i-frame period ~ keyframe interval (lower => faster recovery, slightly more bitrate)
    g_object_set(G_OBJECT(enc),
                 "extra-controls",
                 "controls,video_bitrate=2000000,h264_i_frame_period=15",
                 nullptr);

    // SPS/PPS in stream regularly (helps decoders join mid-stream)
    g_object_set(G_OBJECT(parse),
                 "config-interval", 1,
                 nullptr);

    // Mux alignment helps latency / packetization
    g_object_set(G_OBJECT(mux),
                 "alignment", 7,
                 nullptr);

    // UDP sink: don't sync to clock (avoid latency buffering)
    g_object_set(G_OBJECT(sink),
                 "host", host,
                 "port", port,
                 "sync", FALSE,
                 "async", FALSE,
                 nullptr);

    // Assemble pipeline
    gst_bin_add_many(GST_BIN(pipeline), src, capsfilter, queue, enc, parse, mux, sink, nullptr);

    if (!gst_element_link(src, capsfilter)) {
        std::fprintf(stderr, "Failed to link src -> capsfilter\n");
        return 1;
    }
    if (!gst_element_link(capsfilter, queue)) {
        std::fprintf(stderr, "Failed to link capsfilter -> queue\n");
        return 1;
    }
    if (!gst_element_link(queue, enc)) {
        std::fprintf(stderr, "Failed to link queue -> enc\n");
        return 1;
    }
    if (!gst_element_link(enc, parse)) {
        std::fprintf(stderr, "Failed to link enc -> parse\n");
        return 1;
    }
    if (!gst_element_link(parse, mux)) {
        std::fprintf(stderr, "Failed to link parse -> mux\n");
        return 1;
    }
    if (!gst_element_link(mux, sink)) {
        std::fprintf(stderr, "Failed to link mux -> sink\n");
        return 1;
    }

    // Bus watch
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, nullptr);
    gst_object_unref(bus);

    std::printf("Starting video TX to %s:%d (640x360@60 H264/MPEGTS over UDP)\n", host, port);

    // Start
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
