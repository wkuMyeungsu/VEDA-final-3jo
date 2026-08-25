#include "homography/calibration.hpp"
#include "homography/cli.hpp"
#include "homography/config.hpp"
#include "homography/json.hpp"
#include "homography/render.hpp"

#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
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
            {"marker_shape_rmse_mm", marker.marker_shape_error_mm}});
    const json output_value = {
        {"schema_version", 2}, {"ok", true}, {"map_unit", "mm"},
        {"lens_undistorted", has_active_intrinsics()},
        {"marker_size_mm", layout.value("marker_size_mm", config.manual_solve.marker_size_mm)},
        {"H_camera_pixels_to_channel_map", matrix_to_json(result.h_camera_pixels_to_channel_map)},
        {"H_channel_map_to_camera_pixels", matrix_to_json(result.h_channel_map_to_camera_pixels)},
        {"image_size", {{"width", image.cols}, {"height", image.rows}}},
        {"detected_marker_count", result.detected},
        {"used_marker_count", result.used},
        {"inlier_corner_count", result.inliers},
        {"marker_shape_rmse_mm", result.rmse_mm},
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
              << ", marker-shape RMSE=" << result.rmse_mm << " mm\n";
    return 0;
}

// calibrate-intrinsics 명령으로 ChArUco 보드 사진들에서 카메라 내부 파라미터 산출함.
// 산출물(K, 왜곡계수)은 호모그래피 앱의 undistort 전처리로 이어진다(후속 단계에서 연결).
int calibrate_intrinsics_command(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string images_dir = argument(argc, argv, "--images");
    const std::string output = argument(argc, argv, "--output");
    const std::string preview_path = argument(argc, argv, "--preview");
    if (images_dir.empty() || output.empty())
        throw std::runtime_error("calibrate-intrinsics requires --images and --output");

    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(images_dir)) {
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (extension == ".jpg" || extension == ".jpeg" || extension == ".png")
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    const CameraIntrinsics result = calibrate_camera(config, files);

    // 사용자가 결과를 눈으로 검증할 수 있도록 첫 사진의 왜곡 보정 견본을 남긴다.
    // 보정 전/후 각각에서 DICT_4X4_50(보드 17개) 마커 검출 수를 함께 재서,
    // 왜곡 보정이 검출률을 실제로 올렸는지 숫자로 비교하게 한다.
    json detection = json::object();
    if (!preview_path.empty() && !files.empty()) {
        const int kBoardMarkers = (7 * 5 - 1) / 2;   // ChArUco 7x5 = 17개
        cv::Mat original = cv::imread(files.front());
        std::vector<int> ids;
        std::vector<std::vector<cv::Point2f>> corners, rejected;
        detect_marker_corners(config, original, corners, ids);
        const int before = static_cast<int>(ids.size());

        cv::Mat undistorted;
        cv::undistort(original, undistorted, result.camera_matrix, result.dist_coeffs);
        std::vector<int> undistorted_ids;
        std::vector<std::vector<cv::Point2f>> undistorted_corners;
        detect_marker_corners(config, undistorted, undistorted_corners, undistorted_ids);
        const int after = static_cast<int>(undistorted_ids.size());

        // 보정된 이미지에 검출된 마커를 그려 미리보기로 사용한다.
        cv::aruco::drawDetectedMarkers(undistorted, undistorted_corners, undistorted_ids);
        if (!cv::imwrite(preview_path, undistorted))
            throw std::runtime_error("cannot write preview: " + preview_path);
        detection = {{"board_markers", kBoardMarkers},
                     {"before", before}, {"after", after}};
    }

    json matrix = json::array();
    for (int row = 0; row < result.camera_matrix.rows; ++row)
        matrix.push_back({result.camera_matrix.at<double>(row, 0),
                          result.camera_matrix.at<double>(row, 1),
                          result.camera_matrix.at<double>(row, 2)});
    json coefficients = json::array();
    for (int index = 0; index < result.dist_coeffs.total(); ++index)
        coefficients.push_back(result.dist_coeffs.at<double>(index));
    const json value = {
        {"schema_version", 1}, {"ok", true},
        {"detection", detection},
        {"board", {{"dictionary", config.dictionary},
                   {"squares", {7, 5}}, {"square_mm", 38.0}, {"marker_mm", 19.0}}},
        {"frames_used", result.frames_used},
        {"reprojection_rmse_px", result.reprojection_rmse_px},
        {"camera_matrix", matrix}, {"dist_coeffs", coefficients},
        {"created_utc", utc_now()}};
    std::ofstream output_file(output);
    if (!output_file) throw std::runtime_error("cannot write output: " + output);
    output_file << std::setw(2) << value << '\n';
    std::cout << "calibrated views=" << result.frames_used
              << ", reprojection RMSE=" << result.reprojection_rmse_px << " px\n";
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
    const json value = {{"H_source_camera_pixels_to_destination_camera_pixels", matrix_to_json(h)},
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
        // 설정 파일 옆의 캘리브레이션 산출물이 있으면 렌즈 왜곡 보정을 켠다.
        // 스트림 전용(camera_intrinsics_<stream_id>.json)을 우선하고 공용으로 폴백한다.
        const std::string config_dir =
            fs::path(argument(argc, argv, "--config")).parent_path().string();
        const std::string stream_id = argument(argc, argv, "--stream-id");
        fs::path intrinsics_path = fs::path(config_dir) / "camera_intrinsics.json";
        if (!stream_id.empty() && fs::exists(fs::path(config_dir) /
                                             ("camera_intrinsics_" + stream_id + ".json")))
            intrinsics_path = fs::path(config_dir) / ("camera_intrinsics_" + stream_id + ".json");
        std::ifstream intrinsics_input(intrinsics_path);
        if (intrinsics_input) {
            const json value = json::parse(intrinsics_input);
            cv::Mat camera_matrix(3, 3, CV_64F), dist_coeffs(
                1, static_cast<int>(value.at("dist_coeffs").size()), CV_64F);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    camera_matrix.at<double>(row, col) =
                        value.at("camera_matrix").at(row).at(col).get<double>();
            for (std::size_t index = 0; index < value.at("dist_coeffs").size(); ++index)
                dist_coeffs.at<double>(index) = value.at("dist_coeffs").at(index).get<double>();
            set_active_intrinsics(camera_matrix, dist_coeffs);
        }
        const std::string command = argv[1];
        if (command == "detect-markers") return detect_markers_command(argc, argv);
        if (command == "gen-marker") return generate_marker(argc, argv);
        if (command == "solve-manual") return solve_manual_command(argc, argv);
        if (command == "align-markers") return align_markers_command(argc, argv);
        if (command == "calibrate-intrinsics") return calibrate_intrinsics_command(argc, argv);
        print_usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "homography_tool: " << error.what() << '\n';
        return 1;
    }
}
