#include "homography/calibration.hpp"
#include "homography/cli.hpp"
#include "homography/config.hpp"
#include "homography/json.hpp"
#include "homography/render.hpp"

#include <opencv2/aruco.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace homography {

namespace {

// detect-markers 명령으로 이미지의 ArUco ID와 코너를 추출함.
int detect_markers_command(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string input = argument(argc, argv, "--input");
    const std::string output = argument(argc, argv, "--output");
    const std::string overlay = argument(argc, argv, "--overlay");
    if (input.empty() || output.empty())
        throw std::runtime_error("detect-markers requires --input and --output");
    const cv::Mat image = cv::imread(input);
    if (image.empty()) throw std::runtime_error("cannot read image: " + input);
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    detect_marker_corners(config, image, corners, ids);
    nlohmann::json value = {
        {"image_size", {{"width", image.cols}, {"height", image.rows}}},
        {"ids", ids}, {"corners", nlohmann::json::array()}};
    for (const auto& marker : corners) {
        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : marker)
            points.push_back({{"x", point.x}, {"y", point.y}});
        value["corners"].push_back(points);
    }
    std::ofstream output_file(output);
    if (!output_file) throw std::runtime_error("cannot write output: " + output);
    output_file << std::setw(2) << value << '\n';
    if (!overlay.empty()) {
        cv::Mat annotated = image.clone();
        cv::aruco::drawDetectedMarkers(annotated, corners, ids);
        if (!cv::imwrite(overlay, annotated))
            throw std::runtime_error("cannot write overlay: " + overlay);
    }
    std::cout << "detected " << ids.size() << " markers\n";
    return 0;
}

// gen-marker 명령으로 단일 ArUco 마커 출력물 생성함.
int generate_marker(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string output = argument(argc, argv, "--output");
    if (output.empty()) throw std::runtime_error("gen-marker requires --output");
    const int id = argument(argc, argv, "--id").empty()
        ? -1 : parse_int(argument(argc, argv, "--id"), "id");
    const double size_mm = argument(argc, argv, "--size-mm").empty()
        ? config.marker_output.size_mm
        : parse_double(argument(argc, argv, "--size-mm"), "size-mm");
    const double margin_mm = argument(argc, argv, "--margin-mm").empty()
        ? config.marker_output.margin_mm
        : parse_double(argument(argc, argv, "--margin-mm"), "margin-mm");
    const double dpi = argument(argc, argv, "--dpi").empty()
        ? config.marker_output.dpi
        : parse_double(argument(argc, argv, "--dpi"), "dpi");
    const std::string label = argument(argc, argv, "--label").empty()
        ? config.marker_output.label : argument(argc, argv, "--label");
    if (id < 0 || size_mm <= 0 || margin_mm < 0 || dpi <= 0)
        throw std::runtime_error("invalid marker parameters");
    std::string extension = fs::path(output).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".svg") write_marker_svg(output, config, id, size_mm, margin_mm, label);
    else if (extension == ".png") write_marker_png(output, config, id, size_mm, margin_mm, dpi, label);
    else throw std::runtime_error("gen-marker output must end with .svg or .png");
    std::cout << "marker_id=" << id << ", size=" << size_mm
              << " mm, dpi=" << dpi << ", format=" << extension << "\n";
    return 0;
}

// solve-manual 명령으로 사용자 제공 마커 배치의 호모그래피 저장함.
int solve_manual_command(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string input = argument(argc, argv, "--input");
    const std::string layout_path = argument(argc, argv, "--layout");
    const std::string output = argument(argc, argv, "--output");
    if (input.empty() || layout_path.empty() || output.empty())
        throw std::runtime_error("solve-manual requires --input, --layout and --output");
    const cv::Mat image = cv::imread(input);
    if (image.empty()) throw std::runtime_error("cannot read image: " + input);
    std::ifstream layout_input(layout_path);
    if (!layout_input) throw std::runtime_error("cannot open layout: " + layout_path);
    const json layout = json::parse(layout_input);
    cv::Mat overlay;
    const std::string overlay_path = argument(argc, argv, "--overlay");
    const ManualSolveResult result = solve_manual_image(
        config, image, layout, overlay_path.empty() ? nullptr : &overlay);
    json marker_results = json::array();
    for (const auto& marker : result.markers)
        marker_results.push_back({{"id", marker.id}, {"x_mm", marker.x_mm},
            {"y_mm", marker.y_mm}, {"rotation_deg", marker.rotation_deg},
            {"square_error_mm", marker.square_error_mm}});
    const json output_value = {
        {"schema_version", 2}, {"ok", true}, {"world_unit", "mm"},
        {"marker_size_mm", layout.value("marker_size_mm", config.manual_solve.marker_size_mm)},
        {"H_pixel_to_world", matrix_to_json(result.h_pixel_to_world)},
        {"H_capture_pixel_to_world", matrix_to_json(result.h_pixel_to_world)},
        {"H_world_to_pixel", matrix_to_json(result.h_world_to_pixel)},
        {"image_size", {{"width", image.cols}, {"height", image.rows}}},
        {"detected_marker_count", result.detected},
        {"used_marker_count", result.used},
        {"inlier_corner_count", result.inliers},
        {"reproj_rmse_mm", result.rmse_mm},
        {"axis_max_error_mm", result.axis_max_error_mm},
        {"measurement_rmse_mm", result.measurement_rmse_mm},
        {"measurement_errors_mm", result.measurement_errors_mm},
        {"detected_ids", result.detected_ids}, {"used_ids", result.used_ids},
        {"excluded_ids", result.excluded_ids}, {"suspicious_ids", result.suspicious_ids},
        {"reference_marker_id", result.reference_marker_id},
        {"iterations", result.iterations}, {"markers", marker_results},
        {"layout", layout},
        {"created_utc", utc_now()}};
    std::ofstream output_file(output);
    if (!output_file) throw std::runtime_error("cannot write output: " + output);
    output_file << std::setw(2) << output_value << '\n';
    if (!overlay_path.empty() && !cv::imwrite(overlay_path, overlay))
        throw std::runtime_error("cannot write overlay: " + overlay_path);
    std::cout << "solved markers=" << result.used
              << ", RMSE=" << result.rmse_mm << " mm\n";
    return 0;
}

int align_markers_command(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string source_path = argument(argc, argv, "--source");
    const std::string destination_path = argument(argc, argv, "--destination");
    const std::string output_path = argument(argc, argv, "--output");
    if (source_path.empty() || destination_path.empty() || output_path.empty())
        throw std::runtime_error("align-markers requires --source, --destination and --output");
    const cv::Mat source = cv::imread(source_path);
    const cv::Mat destination = cv::imread(destination_path);
    if (source.empty() || destination.empty()) throw std::runtime_error("cannot read alignment image");
    std::vector<int> common_ids;
    double rmse_px = 0.0;
    const cv::Mat h = align_marker_images(config, source, destination, &common_ids, &rmse_px);
    const json value = {{"H_source_to_destination", matrix_to_json(h)},
        {"source_size", {{"width", source.cols}, {"height", source.rows}}},
        {"destination_size", {{"width", destination.cols}, {"height", destination.rows}}},
        {"common_ids", common_ids}, {"rmse_px", rmse_px}};
    std::ofstream output(output_path);
    if (!output) throw std::runtime_error("cannot write output: " + output_path);
    output << std::setw(2) << value << '\n';
    return 0;
}

}  // namespace

}  // namespace homography

int main(int argc, char** argv) {
    using namespace homography;
    if (argc < 2) {
        print_usage();
        return 1;
    }
    try {
        const std::string command = argv[1];
        if (command == "detect-markers") return detect_markers_command(argc, argv);
        if (command == "gen-marker") return generate_marker(argc, argv);
        if (command == "solve-manual") return solve_manual_command(argc, argv);
        if (command == "align-markers") return align_markers_command(argc, argv);
        print_usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "homography_tool: " << error.what() << '\n';
        return 1;
    }
}
