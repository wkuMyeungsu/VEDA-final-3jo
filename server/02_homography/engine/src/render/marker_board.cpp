#include "homography/render.hpp"

#include "homography/config.hpp"

#include <opencv2/aruco.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace homography {

namespace {

std::string base64(const std::vector<unsigned char>& bytes) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int value = 0;
    int bits = -6;
    for (const unsigned char byte : bytes) {
        value = (value << 8) + byte;
        bits += 8;
        while (bits >= 0) {
            output.push_back(table[(value >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6) output.push_back(table[((value << 8) >> (bits + 8)) & 0x3f]);
    while (output.size() % 4) output.push_back('=');
    return output;
}

std::string marker_data_uri(const Config& config, int id) {
    cv::Mat marker;
    cv::aruco::drawMarker(dictionary(config), id, 200, marker, 1);
    std::vector<unsigned char> encoded;
    cv::imencode(".png", marker, encoded);
    return "data:image/png;base64," + base64(encoded);
}

}  // namespace

void write_marker_svg(const std::string& path, const Config& config, int id,
                      double size_mm, double margin_mm, const std::string& label) {
    const double canvas_mm = size_mm + 2.0 * margin_mm;
    const double stroke_mm = 0.085;
    std::ofstream output(path);
    if (!output) throw std::runtime_error("cannot write output: " + path);
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << canvas_mm
           << "mm\" height=\"" << canvas_mm << "mm\" viewBox=\"0 0 "
           << canvas_mm << " " << canvas_mm << "\">\n"
           << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
           << "<rect x=\"" << stroke_mm / 2.0 << "\" y=\"" << stroke_mm / 2.0
           << "\" width=\"" << canvas_mm - stroke_mm << "\" height=\""
           << canvas_mm - stroke_mm << "\" fill=\"none\" stroke=\"#000\" stroke-width=\""
           << stroke_mm << "\"/>\n"
           << "<path d=\"M " << margin_mm / 2.0 - 3 << " " << margin_mm / 2.0
           << " h 6 M " << margin_mm / 2.0 << " " << margin_mm / 2.0 - 3
           << " v 6\" stroke=\"#336699\" stroke-width=\"0.8\"/>\n"
           << "<text x=\"" << margin_mm * 0.75 << "\" y=\"" << margin_mm * 0.75
           << "\" text-anchor=\"middle\" dominant-baseline=\"middle\""
           << " font-family=\"Arial,sans-serif\" font-size=\"5\" fill=\"#336699\">"
           << label << "</text>\n"
           << "<image x=\"" << margin_mm << "\" y=\"" << margin_mm
           << "\" width=\"" << size_mm << "\" height=\"" << size_mm
           << "\" preserveAspectRatio=\"none\" href=\"" << marker_data_uri(config, id)
           << "\"/>\n</svg>\n";
}

void write_marker_png(const std::string& path, const Config& config, int id,
                      double size_mm, double margin_mm, double dpi,
                      const std::string& label) {
    // 25.4mm/in 변환으로 지정 DPI에서 실제 인쇄 크기 유지함.
    const int pixels = static_cast<int>(std::lround(size_mm * dpi / 25.4));
    const int margin_px = static_cast<int>(std::lround(margin_mm * dpi / 25.4));
    if (pixels <= 0 || margin_px < 0)
        throw std::runtime_error("invalid marker output size");
    cv::Mat marker;
    cv::aruco::drawMarker(dictionary(config), id, pixels, marker, 1);
    cv::Mat image(pixels + 2 * margin_px, pixels + 2 * margin_px, CV_8UC1,
                  cv::Scalar(255));
    marker.copyTo(image(cv::Rect(margin_px, margin_px, pixels, pixels)));
    // 후처리에서 자르기 쉽도록 이미지 가장자리에 1px 외곽선 표시함.
    const int last = image.cols - 1;
    cv::line(image, {0, 0}, {last, 0}, cv::Scalar(0), 1);
    cv::line(image, {0, last}, {last, last}, cv::Scalar(0), 1);
    cv::line(image, {0, 0}, {0, last}, cv::Scalar(0), 1);
    cv::line(image, {last, 0}, {last, last}, cv::Scalar(0), 1);
    if (margin_px > 0)
        cv::drawMarker(image, {margin_px / 2, margin_px / 2}, cv::Scalar(80),
                       cv::MARKER_CROSS, std::max(8, margin_px / 3), 2,
                       cv::LINE_AA);
    if (!label.empty()) {
        const double font_scale = 1.0;
        const int thickness = 1;
        int baseline = 0;
        const cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                                    font_scale, thickness, &baseline);
        const cv::Point center{static_cast<int>(std::lround(margin_px * 0.75)),
                               static_cast<int>(std::lround(margin_px * 0.75))};
        cv::putText(image, label,
                    {center.x - text_size.width / 2,
                     center.y + (text_size.height - baseline) / 2},
                    cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(80), thickness,
                    cv::LINE_AA);
    }
    if (!cv::imwrite(path, image)) throw std::runtime_error("cannot write output: " + path);
}

}  // namespace homography
