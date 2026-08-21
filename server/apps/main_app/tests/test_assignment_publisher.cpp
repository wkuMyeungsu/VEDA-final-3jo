#include <iostream>
#include <string>

#include "network/assignment_publisher.hpp"

int main() {
    const std::string payload = risk_transport::AssignmentPublisher::makePayload(
        "TERM_01", "CAM_01_CH_01", "CAM_01", 1, "2026-08-13T00:00:00.000Z");
    const std::string traced_payload = risk_transport::AssignmentPublisher::makePayload(
        "TERM_01", "CAM_01_CH_01", "CAM_01", 1, "2026-08-13T00:00:00.000Z",
        "server-run-a1", 1, "server-run");
    const bool ok = payload ==
        "{\"type\":\"camera_assignment\",\"terminal_id\":\"TERM_01\","
        "\"stream_id\":\"CAM_01_CH_01\",\"camera_id\":\"CAM_01\","
        "\"channel\":1,\"utc_time\":\"2026-08-13T00:00:00.000Z\"}" &&
        risk_transport::AssignmentPublisher::topicFor("TERM_01") ==
        "forklift/assignment/TERM_01" &&
        risk_transport::AssignmentPublisher::kQos == 1 &&
        risk_transport::AssignmentPublisher::kRetain &&
        traced_payload.find("\"assignment_id\":\"server-run-a1\"") != std::string::npos &&
        traced_payload.find("\"revision\":1") != std::string::npos &&
        traced_payload.find("\"server_run_id\":\"server-run\"") != std::string::npos &&
        traced_payload.find("\"schema_version\":1") != std::string::npos;
    if (!ok) std::cerr << "assignment MQTT 계약 불일치\n";
    return ok ? 0 : 1;
}
