#include "common/types.hpp"

#include <cassert>

int main() {
    using forklift::common::ArucoFrame;
    using forklift::common::ArucoMarker;
    using forklift::common::PixelPoint;

    ArucoFrame empty_frame;
    empty_frame.channel = 1;
    empty_frame.camera_utc_time = "2026-08-04T12:34:56.789Z";
    empty_frame.server_received_utc_time = "2026-08-04T12:34:56.800Z";
    assert(empty_frame.markerCount() == 0);

    ArucoMarker marker;
    marker.marker_id = 40;
    marker.corners = {
        PixelPoint{120.5, 80.0},
        PixelPoint{180.0, 82.0},
        PixelPoint{178.0, 142.5},
        PixelPoint{119.0, 140.0},
    };
    empty_frame.markers.push_back(marker);

    assert(empty_frame.markerCount() == 1);
    assert(empty_frame.markers[0].marker_id == 40);
    assert(empty_frame.markers[0].corners[0].x == 120.5);
    assert(empty_frame.markers[0].corners[3].y == 140.0);
    return 0;
}
