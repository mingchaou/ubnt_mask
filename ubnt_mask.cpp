#include <gst/gst.h>
#include <gst/video/video.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <sstream>

GST_DEBUG_CATEGORY_STATIC(ubnt_mask_debug);
#define GST_CAT_DEFAULT ubnt_mask_debug

#define UBNT_TYPE_MASK (ubnt_mask_get_type())
G_DECLARE_FINAL_TYPE(UbntMask, ubnt_mask, UBNT, MASK, GstElement)

struct _UbntMask {
    GstElement parent;
    GstPad *sinkpad, *srcpad;
    GstVideoInfo info;
    std::vector<std::vector<cv::Point>> mask_points;
};

G_DEFINE_TYPE(UbntMask, ubnt_mask, GST_TYPE_ELEMENT);

static GstFlowReturn ubnt_mask_transform_frame_ip(UbntMask *filter, GstBuffer *buf);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string)NV12, width=(int)[1,MAX], height=(int)[1,MAX], framerate=(fraction)[0/1,MAX], interlace-mode=(string)progressive")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string)NV12, width=(int)[1,MAX], height=(int)[1,MAX], framerate=(fraction)[0/1,MAX], interlace-mode=(string)progressive")
);

enum {
    PROP_0,
    PROP_MASK_POINTS,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void ubnt_mask_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    UbntMask *filter = UBNT_MASK(object);

    switch (prop_id) {
        case PROP_MASK_POINTS: {
            const std::string str = g_value_get_string(value);
            std::vector<std::vector<cv::Point>> polygons;
            std::vector<cv::Point> points;
            std::istringstream polygon_stream(str);
            std::string polygon;

            while (std::getline(polygon_stream, polygon, ';')) {
                std::istringstream point_stream(polygon);
                std::string point_str;

                while (std::getline(point_stream, point_str, ',')) {
                    int x, y;
                    std::istringstream point_iss(point_str);
                    char separator;
                    if (point_iss >> x >> separator >> y && separator == ':') {
                        points.push_back(cv::Point(x, y));
                    }
                }

                if (!points.empty()) {
                    polygons.push_back(points);
                    points.clear();
                }
            }
            
            filter->mask_points = polygons;
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void ubnt_mask_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    UbntMask *filter = UBNT_MASK(object);

    switch (prop_id) {
        case PROP_MASK_POINTS: {
            GString *str = g_string_new(NULL);
            for (const auto &polygon : filter->mask_points) {
                for (const auto &point : polygon) {
                    g_string_append_printf(str, "%d:%d,", point.x, point.y);
                }
                g_string_truncate(str, str->len - 1); // Remove the last comma
                g_string_append(str, ";");
            }
            g_string_truncate(str, str->len - 1); // Remove the last semicolon
            g_value_set_string(value, str->str);
            g_string_free(str, TRUE);
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static gboolean ubnt_mask_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    UbntMask *filter = UBNT_MASK(parent);
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            gst_video_info_from_caps(&filter->info, caps);
            ret = gst_pad_set_caps(filter->srcpad, caps);
            gst_event_unref(event);
            break;
        }
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }
    return ret;
}

static GstFlowReturn ubnt_mask_chain(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    UbntMask *filter = UBNT_MASK(parent);
    return ubnt_mask_transform_frame_ip(filter, buf);
}

static void ubnt_mask_class_init(UbntMaskClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = ubnt_mask_set_property;
    gobject_class->get_property = ubnt_mask_get_property;

    obj_properties[PROP_MASK_POINTS] = g_param_spec_string(
        "mask-points",
        "Mask Points",
        "Coordinates of the mask points in 'x:y,x:y;...' format",
        NULL,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
    );

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);

    gst_element_class_set_details_simple(gstelement_class,
        "UbntMask",
        "Filter/Effect/Video",
        "Applies a mask to the Y and UV planes of NV12 video",
        "Your Name <you@example.com>");
}

static void ubnt_mask_init(UbntMask *filter) {
    filter->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_event_function(filter->sinkpad, GST_DEBUG_FUNCPTR(ubnt_mask_sink_event));
    gst_pad_set_chain_function(filter->sinkpad, GST_DEBUG_FUNCPTR(ubnt_mask_chain));
    GST_PAD_SET_PROXY_CAPS(filter->sinkpad);
    gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

    filter->srcpad = gst_pad_new_from_static_template(&src_template, "src");
    GST_PAD_SET_PROXY_CAPS(filter->srcpad);
    gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);
}

static GstFlowReturn ubnt_mask_transform_frame_ip(UbntMask *filter, GstBuffer *buf) {
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE)) {
        GST_ERROR("Failed to map buffer");
        return GST_FLOW_ERROR;
    }

    int width = GST_VIDEO_INFO_WIDTH(&filter->info);
    int height = GST_VIDEO_INFO_HEIGHT(&filter->info);
    int y_plane_size = width * height;

    if (map.data == NULL) {
        GST_ERROR("Mapped data is NULL");
        gst_buffer_unmap(buf, &map);
        return GST_FLOW_ERROR;
    }

    cv::Mat y_plane(height, width, CV_8UC1, map.data);
    cv::Mat uv_plane(height / 2, width / 2, CV_8UC2, map.data + y_plane_size);

    for (const auto &polygon : filter->mask_points) {
        cv::fillPoly(y_plane, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(0));
    }

    std::vector<std::vector<cv::Point>> uv_points;
    for (const auto &polygon : filter->mask_points) {
        std::vector<cv::Point> scaled_polygon;
        for (auto point : polygon) {
            point.x /= 2;
            point.y /= 2;
            scaled_polygon.push_back(point);
        }
        uv_points.push_back(scaled_polygon);
    }

    for (const auto &polygon : uv_points) {
        cv::fillPoly(uv_plane, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(128, 128));
    }

    gst_buffer_unmap(buf, &map);
    return gst_pad_push(filter->srcpad, buf);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean ubnt_mask_plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(ubnt_mask_debug, "ubnt_mask", 0, "Applies a mask to the Y and UV planes of NV12 video");

    return gst_element_register(plugin, "ubnt_mask", GST_RANK_NONE, UBNT_TYPE_MASK);
}

#define PACKAGE "ubntmask"

/* gstreamer looks for this structure to register filters */
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ubntmask,
    "Applies a mask to the Y and UV planes of NV12 video",
    ubnt_mask_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
