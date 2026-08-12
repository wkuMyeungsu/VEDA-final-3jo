#include "homography/calibration.hpp"
#include "homography/cli.hpp"
#include "homography/config.hpp"
#include "homography/json.hpp"
#include "homography/render.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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

// calibrate 명령으로 자동 검출 기반 호모그래피 저장함.
int calibrate_command(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string input = argument(argc, argv, "--input");
    const std::string output = argument(argc, argv, "--output");
    if (input.empty() || output.empty())
        throw std::runtime_error("calibrate requires --input and --output");
    const cv::Mat image = cv::imread(input);
    if (image.empty()) throw std::runtime_error("cannot read image: " + input);
    const double gate = argument(argc, argv, "--max-rmse-cm").empty()
        ? config.calibration.max_rmse_cm
        : parse_double(argument(argc, argv, "--max-rmse-cm"), "max-rmse-cm");
    const DetectionResult detection = calibrate_image(config, image);
    if (detection.rmse_cm > gate) {
        std::cerr << "RMSE gate failed: " << detection.rmse_cm << " cm > "
                  << gate << " cm\n";
        return 2;
    }
    const int channel = argument(argc, argv, "--channel").empty()
        ? config.calibration.channel
        : parse_int(argument(argc, argv, "--channel"), "channel");
    write_calibration(output, config, detection, image.size(), channel, gate);
    std::cout << "calibrated " << detection.ids.size()
              << " markers, RMSE=" << detection.rmse_cm << " cm\n";
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
    const json output_value = {
        {"schema_version", 1}, {"ok", true},
        {"marker_size_mm", layout.value("marker_size_mm", config.manual_solve.marker_size_mm)},
        {"H_pixel_to_world", matrix_to_json(result.h_pixel_to_world)},
        {"H_world_to_pixel", matrix_to_json(result.h_world_to_pixel)},
        {"image_size", {{"width", image.cols}, {"height", image.rows}}},
        {"detected_marker_count", result.detected},
        {"used_marker_count", result.used},
        {"inlier_corner_count", result.inliers},
        {"reproj_rmse_mm", result.rmse_mm},
        {"detected_ids", result.detected_ids}, {"used_ids", result.used_ids},
        {"missing_ids", result.missing_ids}, {"layout", layout},
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

// view 명령으로 원근 보정 결과와 마커별 오차 시각화함.
int view_command(int argc, char** argv) {
    const Config config = read_config(argument(argc, argv, "--config"));
    const std::string homography_path = argument(argc, argv, "--homography");
    const std::string input = argument(argc, argv, "--input");
    if (homography_path.empty() || input.empty())
        throw std::runtime_error("view requires --homography and --input");
    const cv::Mat image = cv::imread(input);
    if (image.empty()) throw std::runtime_error("cannot read image");
    const json value = read_homography(homography_path);
    const cv::Mat homography = json_to_matrix(value.at("H_pixel_to_world"));
    const int scale = config.preview.scale;
    const int width = static_cast<int>(config.cols *
        (config.marker_len_cm + config.gap_cm) * scale);
    const int height = static_cast<int>(config.rows *
        (config.marker_len_cm + config.gap_cm) * scale);
    const cv::Mat world_scale = (cv::Mat_<double>(3, 3) <<
        20, 0, 0, 0, 20, 0, 0, 0, 1);
    cv::Mat top;
    cv::warpPerspective(image, top, world_scale * homography,
                        cv::Size(width, height));
    cv::Mat errors = top.clone();
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    const DetectionResult detection = calibrate_image(config, gray);
    double sum = 0.0;
    for (size_t i = 0; i < detection.pixels.size(); ++i) {
        std::vector<cv::Point2f> projected;
        const std::vector<cv::Point2f> source{detection.pixels[i]};
        cv::perspectiveTransform(source, projected, homography);
        const double dx = projected[0].x - detection.worlds[i].x;
        const double dy = projected[0].y - detection.worlds[i].y;
        const double error_cm = std::sqrt(dx * dx + dy * dy);
        sum += error_cm * error_cm;
        const cv::Scalar color = error_cm < config.preview.good_error_cm
            ? cv::Scalar(0, 200, 0)
            : (error_cm < config.preview.warning_error_cm
                ? cv::Scalar(0, 220, 220) : cv::Scalar(0, 0, 255));
        std::vector<cv::Point2f> screen;
        const std::vector<cv::Point2f> world_point{detection.worlds[i]};
        cv::perspectiveTransform(world_point, screen, world_scale);
        cv::circle(errors, screen[0], 5, color, -1);
    }
    const int pitch_px = static_cast<int>(
        (config.marker_len_cm + config.gap_cm) * config.preview.scale);
    for (int col = 0; col <= config.cols; ++col)
        cv::line(errors, {col * pitch_px, 0}, {col * pitch_px, height - 1},
                 cv::Scalar(0, 180, 0), 1);
    for (int row = 0; row <= config.rows; ++row)
        cv::line(errors, {0, row * pitch_px}, {width - 1, row * pitch_px},
                 cv::Scalar(0, 180, 0), 1);
    const std::string directory = argument(argc, argv, "--output-dir");
    if (!directory.empty()) {
        fs::create_directories(directory);
        cv::imwrite((fs::path(directory) / "point-error.png").string(), errors);
        cv::imwrite((fs::path(directory) / "topdown-warp.png").string(), top);
    }
    const double rmse = detection.pixels.empty() ? 0.0
        : std::sqrt(sum / detection.pixels.size());
    cv::putText(errors, "RMSE=" + std::to_string(rmse) + " cm", {10, 25},
                cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(20, 20, 20), 2);
    std::cout << "RMSE=" << rmse << " cm\n";
    if (has_argument(argc, argv, "--live")) {
        cv::imshow("point error", errors);
        cv::imshow("top-down warp", top);
        cv::waitKey(0);
    }
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
        if (command == "gen-marker") return generate_marker(argc, argv);
        if (command == "calibrate") return calibrate_command(argc, argv);
        if (command == "solve-manual") return solve_manual_command(argc, argv);
        if (command == "view") return view_command(argc, argv);
        print_usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "homography_tool: " << error.what() << '\n';
        return 1;
    }
}
