#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct Config {
    std::string dictionary = "DICT_4X4_50";
    int cols = 0, rows = 0, id_offset = 0;
    double marker_len_cm = 0.0, gap_cm = 0.0;
    std::string origin_corner = "TL";
};

struct DetectionResult {
    cv::Mat h_pixel_to_world;
    cv::Mat h_world_to_pixel;
    std::vector<int> ids;
    std::vector<cv::Point2f> pixels;
    std::vector<cv::Point2f> worlds;
    int inliers = 0;
    double rmse_cm = std::numeric_limits<double>::infinity();
};

struct BoardLayout {
    double width_mm = 0.0;
    double height_mm = 0.0;
    double margin_mm = 10.0;
    double origin_x_mm = 0.0;
    double origin_y_mm = 0.0;
    double dpi = 300.0;
    bool show_ids = false;
    bool show_origin = true;
    bool show_grid = false;
};

void usage() {
    std::cout << R"(homography_tool <command> [options]
commands:
  gen-board  --config CONFIG --output FILE
             [--board-width-mm MM --board-height-mm MM --margin-mm MM]
             [--dpi DPI] [--show-ids] [--show-grid] [--no-origin]
  gen-marker --config CONFIG --id ID --output FILE
             [--size-mm MM] [--margin-mm MM] [--label TEXT] [--dpi DPI]
  calibrate  --config CONFIG --input IMAGE --output JSON [--channel N]
             [--max-rmse-cm CM]
  solve-manual --config CONFIG --input IMAGE --layout JSON --output JSON
               [--overlay IMAGE]
  view       --config CONFIG --homography JSON --input IMAGE
             [--output-dir DIR] [--live]
  selftest   [--verbose]
)";
}

std::string arg(int argc, char** argv, const std::string& name, const std::string& fallback = "") {
    for (int i = 2; i < argc; ++i) {
        if (argv[i] == name && i + 1 < argc) return argv[i + 1];
    }
    return fallback;
}

bool has_arg(int argc, char** argv, const std::string& name) {
    for (int i = 2; i < argc; ++i) if (argv[i] == name) return true;
    return false;
}

int to_int(const std::string& s, const char* label) {
    try { return std::stoi(s); } catch (...) { throw std::runtime_error(std::string("invalid ") + label); }
}

double to_double(const std::string& s, const char* label) {
    try { return std::stod(s); } catch (...) { throw std::runtime_error(std::string("invalid ") + label); }
}

Config read_config(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open config: " + path);
    const json j = json::parse(in);
    Config c;
    c.dictionary = j.at("dictionary").get<std::string>();
    c.cols = j.at("cols").get<int>(); c.rows = j.at("rows").get<int>();
    c.marker_len_cm = j.at("marker_len_cm").get<double>();
    c.gap_cm = j.at("gap_cm").get<double>(); c.id_offset = j.at("id_offset").get<int>();
    c.origin_corner = j.value("origin_corner", "TL");
    if (c.cols <= 0 || c.rows <= 0 || c.marker_len_cm <= 0 || c.gap_cm < 0)
        throw std::runtime_error("invalid grid dimensions or lengths");
    if (c.origin_corner != "TL") throw std::runtime_error("only origin_corner=TL is supported");
    return c;
}

cv::Ptr<cv::aruco::Dictionary> dictionary(const Config& c) {
    static const std::map<std::string, cv::aruco::PREDEFINED_DICTIONARY_NAME> names = {
        {"DICT_4X4_50", cv::aruco::DICT_4X4_50}, {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
        {"DICT_5X5_50", cv::aruco::DICT_5X5_50}, {"DICT_6X6_50", cv::aruco::DICT_6X6_50},
        {"DICT_7X7_50", cv::aruco::DICT_7X7_50}
    };
    const auto it = names.find(c.dictionary);
    if (it == names.end()) throw std::runtime_error("unsupported dictionary: " + c.dictionary);
    return cv::aruco::getPredefinedDictionary(it->second);
}

std::vector<cv::Point2f> world_corners(const Config& c, int id) {
    const int n = id - c.id_offset;
    if (n < 0 || n >= c.rows * c.cols) return {};
    const int row = n / c.cols, col = n % c.cols;
    const double pitch = c.marker_len_cm + c.gap_cm;
    const double x = col * pitch, y = row * pitch, l = c.marker_len_cm;
    return {{static_cast<float>(x), static_cast<float>(y)},
            {static_cast<float>(x+l), static_cast<float>(y)},
            {static_cast<float>(x+l), static_cast<float>(y+l)},
            {static_cast<float>(x), static_cast<float>(y+l)}};
}

json matrix_json(const cv::Mat& m) {
    json a = json::array();
    for (int r = 0; r < m.rows; ++r) { json row = json::array(); for (int col = 0; col < m.cols; ++col) row.push_back(m.at<double>(r,col)); a.push_back(row); }
    return a;
}

cv::Mat json_matrix(const json& a) {
    cv::Mat m(3, 3, CV_64F);
    for (int r=0;r<3;++r) for (int c=0;c<3;++c) m.at<double>(r,c)=a.at(r).at(c).get<double>();
    return m;
}

std::string utc_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{}; gmtime_r(&now, &tm); std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ"); return os.str();
}

cv::Mat render_board(const Config& c, int scale = 20) {
    const int width = static_cast<int>((c.cols * (c.marker_len_cm+c.gap_cm)-c.gap_cm)*scale) + 2*scale;
    const int height = static_cast<int>((c.rows * (c.marker_len_cm+c.gap_cm)-c.gap_cm)*scale) + 2*scale;
    cv::Mat board(height, width, CV_8UC1, cv::Scalar(255));
    auto dict = dictionary(c);
    for (int row=0; row<c.rows; ++row) for (int col=0; col<c.cols; ++col) {
        cv::Mat marker; cv::aruco::drawMarker(dict, c.id_offset+row*c.cols+col,
            static_cast<int>(c.marker_len_cm*scale), marker, 1);
        const int x=scale+static_cast<int>(col*(c.marker_len_cm+c.gap_cm)*scale);
        const int y=scale+static_cast<int>(row*(c.marker_len_cm+c.gap_cm)*scale);
        marker.copyTo(board(cv::Rect(x,y,marker.cols,marker.rows)));
    }
    return board;
}

double grid_width_mm(const Config& c) { return c.cols*c.marker_len_cm*10.0 + (c.cols-1)*c.gap_cm*10.0; }
double grid_height_mm(const Config& c) { return c.rows*c.marker_len_cm*10.0 + (c.rows-1)*c.gap_cm*10.0; }

BoardLayout read_layout(const Config& c, int argc, char** argv) {
    BoardLayout b;
    if (!arg(argc,argv,"--margin-mm").empty()) b.margin_mm=to_double(arg(argc,argv,"--margin-mm"),"margin-mm");
    b.width_mm = arg(argc,argv,"--board-width-mm").empty() ? grid_width_mm(c)+2*b.margin_mm : to_double(arg(argc,argv,"--board-width-mm"),"board-width-mm");
    b.height_mm = arg(argc,argv,"--board-height-mm").empty() ? grid_height_mm(c)+2*b.margin_mm : to_double(arg(argc,argv,"--board-height-mm"),"board-height-mm");
    if (!arg(argc,argv,"--dpi").empty()) b.dpi=to_double(arg(argc,argv,"--dpi"),"dpi");
    b.show_ids=has_arg(argc,argv,"--show-ids"); b.show_origin=!has_arg(argc,argv,"--no-origin"); b.show_grid=has_arg(argc,argv,"--show-grid");
    if (b.width_mm<=0||b.height_mm<=0||b.margin_mm<0||b.dpi<=0) throw std::runtime_error("invalid board layout");
    if (grid_width_mm(c)+2*b.margin_mm>b.width_mm || grid_height_mm(c)+2*b.margin_mm>b.height_mm)
        throw std::runtime_error("grid does not fit in board size with the requested margin");
    b.origin_x_mm = (b.width_mm - grid_width_mm(c)) / 2.0;
    b.origin_y_mm = (b.height_mm - grid_height_mm(c)) / 2.0;
    return b;
}

std::string base64(const std::vector<unsigned char>& bytes) {
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; int val=0,bits=-6;
    for(unsigned char ch:bytes){val=(val<<8)+ch;bits+=8;while(bits>=0){out.push_back(table[(val>>bits)&0x3F]);bits-=6;}}
    if(bits>-6) out.push_back(table[((val<<8)>>(bits+8))&0x3F]); while(out.size()%4) out.push_back('='); return out;
}

std::string marker_data_uri(const Config& c, int id) {
    cv::Mat marker; cv::aruco::drawMarker(dictionary(c),id,200,marker,1); std::vector<unsigned char> encoded;
    cv::imencode(".png",marker,encoded); return "data:image/png;base64,"+base64(encoded);
}

void write_marker_svg(const std::string& path, const Config& c, int id, double size_mm,
                      double margin_mm, const std::string& label) {
    const double canvas_mm=size_mm+2.0*margin_mm;
    const double crop_stroke_mm=0.085;
    std::ofstream out(path); if(!out) throw std::runtime_error("cannot write output: "+path);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << canvas_mm
        << "mm\" height=\"" << canvas_mm << "mm\" viewBox=\"0 0 "
        << canvas_mm << " " << canvas_mm << "\">\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
        << "<rect x=\"" << crop_stroke_mm/2.0 << "\" y=\"" << crop_stroke_mm/2.0
        << "\" width=\"" << canvas_mm-crop_stroke_mm << "\" height=\""
        << canvas_mm-crop_stroke_mm << "\" fill=\"none\" stroke=\"#000\" stroke-width=\""
        << crop_stroke_mm << "\"/>\n"
        << "<path d=\"M " << margin_mm/2.0-3 << " " << margin_mm/2.0
        << " h 6 M " << margin_mm/2.0 << " " << margin_mm/2.0-3
        << " v 6\" stroke=\"#336699\" stroke-width=\"0.8\"/>\n"
        << "<text x=\"" << margin_mm*0.75 << "\" y=\"" << margin_mm*0.75
        << "\" text-anchor=\"middle\" dominant-baseline=\"middle\""
        << " font-family=\"Arial,sans-serif\" font-size=\"5\" fill=\"#336699\">"
        << label << "</text>\n"
        << "<image x=\"" << margin_mm << "\" y=\"" << margin_mm << "\" width=\"" << size_mm
        << "\" height=\"" << size_mm << "\" preserveAspectRatio=\"none\" href=\""
        << marker_data_uri(c,id) << "\"/>\n</svg>\n";
}

void write_marker_png(const std::string& path, const Config& c, int id, double size_mm,
                      double margin_mm, double dpi, const std::string& label) {
    const int pixels=static_cast<int>(std::lround(size_mm*dpi/25.4));
    const int margin_px=static_cast<int>(std::lround(margin_mm*dpi/25.4));
    if(pixels<=0 || margin_px<0) throw std::runtime_error("invalid marker output size");
    cv::Mat marker; cv::aruco::drawMarker(dictionary(c),id,pixels,marker,1);
    cv::Mat image(pixels+2*margin_px,pixels+2*margin_px,CV_8UC1,cv::Scalar(255));
    marker.copyTo(image(cv::Rect(margin_px,margin_px,pixels,pixels)));
    const int last=image.cols-1;
    cv::line(image,{0,0},{last,0},cv::Scalar(0),1);
    cv::line(image,{0,last},{last,last},cv::Scalar(0),1);
    cv::line(image,{0,0},{0,last},cv::Scalar(0),1);
    cv::line(image,{last,0},{last,last},cv::Scalar(0),1);
    if(margin_px>0) cv::drawMarker(image,{margin_px/2,margin_px/2},cv::Scalar(80),
        cv::MARKER_CROSS,std::max(8,margin_px/3),2,cv::LINE_AA);
    if(!label.empty()) {
        const double font_scale=1.0; const int thickness=1;
        int baseline=0; const cv::Size text_size=cv::getTextSize(label,cv::FONT_HERSHEY_SIMPLEX,font_scale,thickness,&baseline);
        const cv::Point center{static_cast<int>(std::lround(margin_px*0.75)),
                               static_cast<int>(std::lround(margin_px*0.75))};
        cv::putText(image,label,{center.x-text_size.width/2,center.y+(text_size.height-baseline)/2},
            cv::FONT_HERSHEY_SIMPLEX,font_scale,cv::Scalar(80),thickness,cv::LINE_AA);
    }
    if(!cv::imwrite(path,image)) throw std::runtime_error("cannot write output: "+path);
}

void write_board_svg(const std::string& path, const Config& c, const BoardLayout& b) {
    const double marker_mm=c.marker_len_cm*10.0, pitch_mm=(c.marker_len_cm+c.gap_cm)*10.0;
    std::ofstream out(path); if(!out) throw std::runtime_error("cannot write output: "+path);
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<b.width_mm<<"mm\" height=\""<<b.height_mm<<"mm\" viewBox=\"0 0 "<<b.width_mm<<" "<<b.height_mm<<"\">\n";
    out<<"<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    if(b.show_grid){out<<"<g fill=\"none\" stroke=\"#55aa55\" stroke-width=\"0.25\">"; for(int col=0;col<=c.cols;++col) out<<"<line x1=\""<<b.origin_x_mm+col*pitch_mm<<"\" y1=\""<<b.origin_y_mm<<"\" x2=\""<<b.origin_x_mm+col*pitch_mm<<"\" y2=\""<<b.origin_y_mm+grid_height_mm(c)<<"\"/>"; for(int row=0;row<=c.rows;++row) out<<"<line x1=\""<<b.origin_x_mm<<"\" y1=\""<<b.origin_y_mm+row*pitch_mm<<"\" x2=\""<<b.origin_x_mm+grid_width_mm(c)<<"\" y2=\""<<b.origin_y_mm+row*pitch_mm<<"\"/>"; out<<"</g>\n";}
    for(int row=0;row<c.rows;++row) for(int col=0;col<c.cols;++col){const int id=c.id_offset+row*c.cols+col; const double x=b.origin_x_mm+col*pitch_mm,y=b.origin_y_mm+row*pitch_mm; out<<"<image x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<marker_mm<<"\" height=\""<<marker_mm<<"\" preserveAspectRatio=\"none\" href=\""<<marker_data_uri(c,id)<<"\"/>\n"; out<<"<path d=\"M "<<x-3<<" "<<y-2<<" h 4 M "<<x-1<<" "<<y-4<<" v 4\" stroke=\"#0066cc\" stroke-width=\"0.3\"/>\n"; if(b.show_ids) out<<"<text x=\""<<x<<"\" y=\""<<y+marker_mm+4<<"\" font-size=\"3.5\" fill=\"#111\">id="<<id<<"</text>\n";}
    if(b.show_origin) out<<"<text x=\""<<b.origin_x_mm<<"\" y=\""<<b.origin_y_mm-3<<"\" font-size=\"4\" fill=\"#0066cc\">ORIGIN (0,0) / X-right / Y-down</text>\n";
    out<<"</svg>\n";
}

void write_board_png(const std::string& path, const Config& c, const BoardLayout& b) {
    const double pxmm=b.dpi/25.4, marker_px=c.marker_len_cm*10.0*pxmm, pitch_px=(c.marker_len_cm+c.gap_cm)*10.0*pxmm;
    cv::Mat image(static_cast<int>(std::lround(b.height_mm*pxmm)),static_cast<int>(std::lround(b.width_mm*pxmm)),CV_8UC3,cv::Scalar(255,255,255));
    for(int row=0;row<c.rows;++row) for(int col=0;col<c.cols;++col){int id=c.id_offset+row*c.cols+col; cv::Mat marker;cv::aruco::drawMarker(dictionary(c),id,static_cast<int>(std::lround(marker_px)),marker,1); cv::Mat color;cv::cvtColor(marker,color,cv::COLOR_GRAY2BGR); int x=std::lround((b.origin_x_mm+col*(c.marker_len_cm+c.gap_cm)*10.0)*pxmm),y=std::lround((b.origin_y_mm+row*(c.marker_len_cm+c.gap_cm)*10.0)*pxmm); color.copyTo(image(cv::Rect(x,y,color.cols,color.rows))); if(b.show_origin) cv::drawMarker(image,{x-static_cast<int>(2*pxmm),y-static_cast<int>(2*pxmm)},cv::Scalar(200,0,0),cv::MARKER_CROSS,static_cast<int>(4*pxmm),1); if(b.show_ids) cv::putText(image,"id="+std::to_string(id),{x,y+color.rows+static_cast<int>(4*pxmm)},cv::FONT_HERSHEY_SIMPLEX,0.8*pxmm,cv::Scalar(20,20,20),1,cv::LINE_AA);}
    if (b.show_grid) { const int left=std::lround(b.origin_x_mm*pxmm), top=std::lround(b.origin_y_mm*pxmm), right=std::lround((b.origin_x_mm+grid_width_mm(c))*pxmm), bottom=std::lround((b.origin_y_mm+grid_height_mm(c))*pxmm); for(int col=0;col<=c.cols;++col){int x=std::lround((b.origin_x_mm+col*(c.marker_len_cm+c.gap_cm)*10.0)*pxmm);cv::line(image,{x,top},{x,bottom},cv::Scalar(140,210,140),1);} for(int row=0;row<=c.rows;++row){int y=std::lround((b.origin_y_mm+row*(c.marker_len_cm+c.gap_cm)*10.0)*pxmm);cv::line(image,{left,y},{right,y},cv::Scalar(140,210,140),1);} }
    if(!cv::imwrite(path,image)) throw std::runtime_error("cannot write output: "+path);
}

DetectionResult calibrate_image(const Config& c, const cv::Mat& image) {
    DetectionResult out; auto dict = dictionary(c);
    std::vector<int> ids; std::vector<std::vector<cv::Point2f>> corners, rejected;
    cv::aruco::detectMarkers(image, dict, corners, ids);
    std::vector<cv::Point2f> px, world; std::vector<int> valid;
    for (size_t i=0;i<ids.size();++i) {
        const auto wc = world_corners(c, ids[i]); if (wc.empty()) continue;
        for (int k=0;k<4;++k) { px.push_back(corners[i][k]); world.emplace_back(static_cast<float>(wc[k].x),static_cast<float>(wc[k].y)); }
        valid.push_back(ids[i]);
    }
    if (px.size() < 8) throw std::runtime_error("fewer than two valid markers detected");
    cv::Mat mask; out.h_pixel_to_world = cv::findHomography(px, world, cv::RANSAC, 3.0, mask);
    if (out.h_pixel_to_world.empty()) throw std::runtime_error("findHomography failed");
    out.h_pixel_to_world /= out.h_pixel_to_world.at<double>(2,2);
    out.h_world_to_pixel = out.h_pixel_to_world.inv();
    double sum=0; int count=0;
    for (int i=0;i<static_cast<int>(px.size());++i) if (mask.at<uchar>(i)) {
        std::vector<cv::Point2f> a{px[i]}, b; cv::perspectiveTransform(a,b,out.h_pixel_to_world);
        const double dx=b[0].x-world[i].x, dy=b[0].y-world[i].y; sum += dx*dx+dy*dy; ++count;
    }
    out.inliers=count; out.rmse_cm=count ? std::sqrt(sum/count) : std::numeric_limits<double>::infinity();
    out.ids=valid; out.pixels=px; out.worlds=world; return out;
}

struct ManualSolveResult {
    cv::Mat h_pixel_to_world;
    cv::Mat h_world_to_pixel;
    int detected = 0;
    int used = 0;
    int inliers = 0;
    double rmse_mm = std::numeric_limits<double>::infinity();
    std::vector<int> detected_ids;
    std::vector<int> used_ids;
    std::vector<int> missing_ids;
};

ManualSolveResult solve_manual_image(const Config& c, const cv::Mat& image, const json& layout,
                                     cv::Mat* overlay) {
    const double side_mm = layout.value("marker_size_mm", 100.0);
    if (side_mm <= 0.0) throw std::runtime_error("marker_size_mm must be positive");
    if (!layout.contains("markers") || !layout.at("markers").is_array())
        throw std::runtime_error("layout.markers must be an array");
    std::map<int, std::pair<double,double>> positions;
    for (const auto& item : layout.at("markers")) {
        const int id = item.at("id").get<int>();
        positions[id] = {item.at("x_mm").get<double>(), item.at("y_mm").get<double>()};
    }

    ManualSolveResult out;
    auto dict = dictionary(c);
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    cv::aruco::detectMarkers(image, dict, corners, ids);
    out.detected = static_cast<int>(ids.size());
    out.detected_ids = ids;
    std::vector<cv::Point2f> pixels, worlds;
    std::vector<int> used_ids;
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto it = positions.find(ids[i]);
        if (it == positions.end() || corners[i].size() != 4) continue;
        const double x = it->second.first, y = it->second.second;
        const std::vector<cv::Point2f> wc{{static_cast<float>(x), static_cast<float>(y)},
            {static_cast<float>(x + side_mm), static_cast<float>(y)},
            {static_cast<float>(x + side_mm), static_cast<float>(y + side_mm)},
            {static_cast<float>(x), static_cast<float>(y + side_mm)}};
        for (int k = 0; k < 4; ++k) { pixels.push_back(corners[i][k]); worlds.push_back(wc[k]); }
        used_ids.push_back(ids[i]);
    }
    out.used = static_cast<int>(used_ids.size());
    out.used_ids = used_ids;
    for (const auto& p : positions)
        if (std::find(ids.begin(), ids.end(), p.first) == ids.end()) out.missing_ids.push_back(p.first);
    if (pixels.size() < 4) throw std::runtime_error("at least one valid marker is required");
    cv::Mat mask;
    out.h_pixel_to_world = cv::findHomography(pixels, worlds, cv::RANSAC, 3.0, mask);
    if (out.h_pixel_to_world.empty()) throw std::runtime_error("findHomography failed");
    out.h_pixel_to_world /= out.h_pixel_to_world.at<double>(2,2);
    out.h_world_to_pixel = out.h_pixel_to_world.inv();
    double sum = 0.0; int count = 0;
    cv::Mat annotated;
    if (overlay) { if (image.channels() == 1) cv::cvtColor(image, annotated, cv::COLOR_GRAY2BGR); else annotated = image.clone(); }
    for (int i = 0; i < static_cast<int>(pixels.size()); ++i) {
        std::vector<cv::Point2f> projected;
        cv::perspectiveTransform(std::vector<cv::Point2f>{pixels[i]}, projected, out.h_pixel_to_world);
        const double dx = projected[0].x - worlds[i].x, dy = projected[0].y - worlds[i].y;
        const double error = std::sqrt(dx*dx + dy*dy);
        if (mask.empty() || mask.at<uchar>(i)) { sum += error*error; ++count; }
        if (overlay) cv::circle(annotated, pixels[i], 5,
            error <= 2.0 ? cv::Scalar(0,200,0) : cv::Scalar(0,0,255), 2);
    }
    out.inliers = count;
    out.rmse_mm = count ? std::sqrt(sum / count) : std::numeric_limits<double>::infinity();
    if (overlay) {
        for (size_t i = 0; i < ids.size(); ++i)
            if (positions.count(ids[i]) && corners[i].size() == 4)
                cv::putText(annotated, "ID " + std::to_string(ids[i]), corners[i][0] + cv::Point2f(4, -6),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255,80,0), 2, cv::LINE_AA);
        cv::putText(annotated, "RMSE=" + std::to_string(out.rmse_mm) + " mm", {12,28},
            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(20,20,20), 2, cv::LINE_AA);
        *overlay = annotated;
    }
    return out;
}

int solve_manual(int argc, char** argv) {
    const Config c = read_config(arg(argc,argv,"--config"));
    const std::string input = arg(argc,argv,"--input"), layout_path = arg(argc,argv,"--layout"), output = arg(argc,argv,"--output");
    if (input.empty() || layout_path.empty() || output.empty()) throw std::runtime_error("solve-manual requires --input, --layout and --output");
    const cv::Mat image = cv::imread(input); if (image.empty()) throw std::runtime_error("cannot read image: " + input);
    std::ifstream in(layout_path); if (!in) throw std::runtime_error("cannot open layout: " + layout_path);
    const json layout = json::parse(in);
    cv::Mat overlay;
    const std::string overlay_path = arg(argc,argv,"--overlay");
    ManualSolveResult d = solve_manual_image(c, image, layout, overlay_path.empty() ? nullptr : &overlay);
    json result = {{"schema_version", 1}, {"ok", true}, {"marker_size_mm", layout.value("marker_size_mm", 100.0)},
        {"H_pixel_to_world", matrix_json(d.h_pixel_to_world)}, {"H_world_to_pixel", matrix_json(d.h_world_to_pixel)},
        {"image_size", {{"width", image.cols}, {"height", image.rows}}}, {"detected_marker_count", d.detected},
        {"used_marker_count", d.used}, {"inlier_corner_count", d.inliers}, {"reproj_rmse_mm", d.rmse_mm},
        {"detected_ids", d.detected_ids}, {"used_ids", d.used_ids}, {"missing_ids", d.missing_ids},
        {"layout", layout}, {"created_utc", utc_now()}};
    std::ofstream out(output); if (!out) throw std::runtime_error("cannot write output: " + output); out << std::setw(2) << result << '\n';
    if (!overlay_path.empty() && !cv::imwrite(overlay_path, overlay)) throw std::runtime_error("cannot write overlay: " + overlay_path);
    std::cout << "solved markers=" << d.used << ", RMSE=" << d.rmse_mm << " mm\n";
    return 0;
}

json config_json(const Config& c) {
    return {{"dictionary",c.dictionary},{"cols",c.cols},{"rows",c.rows},{"marker_len_cm",c.marker_len_cm},
            {"gap_cm",c.gap_cm},{"id_offset",c.id_offset},{"origin_corner",c.origin_corner}};
}

void write_calibration(const std::string& path, const Config& c, const DetectionResult& d, const cv::Size& size, int channel, double gate) {
    json j={{"schema_version",1},{"channel",channel},{"H_pixel_to_world",matrix_json(d.h_pixel_to_world)},
        {"H_world_to_pixel",matrix_json(d.h_world_to_pixel)},{"image_size",{{"width",size.width},{"height",size.height}}},
        {"dictionary",c.dictionary},{"grid",config_json(c)},{"inliers",d.inliers},{"reproj_rmse_cm",d.rmse_cm},
        {"rmse_gate_cm",gate},{"created_utc",utc_now()}};
    std::ofstream out(path); if (!out) throw std::runtime_error("cannot write output: "+path); out << std::setw(2) << j << '\n';
}

int gen_board(int argc,char** argv) {
    const Config c=read_config(arg(argc,argv,"--config")); const std::string output=arg(argc,argv,"--output");
    if (output.empty()) throw std::runtime_error("gen-board requires --output");
    const BoardLayout b=read_layout(c,argc,argv); std::string extension=fs::path(output).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    if (extension==".svg") write_board_svg(output,c,b); else if(extension==".png") write_board_png(output,c,b); else throw std::runtime_error("gen-board output must be .svg or .png");
    std::cout<<"board="<<b.width_mm<<"x"<<b.height_mm<<" mm, dpi="<<b.dpi<<", format="<<extension<<"\n"; return 0;
}

int gen_marker(int argc,char** argv) {
    const Config c=read_config(arg(argc,argv,"--config"));
    const std::string output=arg(argc,argv,"--output");
    if(output.empty()) throw std::runtime_error("gen-marker requires --output");
    const int id=arg(argc,argv,"--id").empty()? -1 : to_int(arg(argc,argv,"--id"),"id");
    const double size_mm=arg(argc,argv,"--size-mm").empty()?100.0:to_double(arg(argc,argv,"--size-mm"),"size-mm");
    const double margin_mm=arg(argc,argv,"--margin-mm").empty()?0.0:to_double(arg(argc,argv,"--margin-mm"),"margin-mm");
    const double dpi=arg(argc,argv,"--dpi").empty()?300.0:to_double(arg(argc,argv,"--dpi"),"dpi");
    const std::string label=arg(argc,argv,"--label");
    if(id<0 || size_mm<=0 || margin_mm<0 || dpi<=0) throw std::runtime_error("invalid marker parameters");
    std::string extension=fs::path(output).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    if(extension==".svg") write_marker_svg(output,c,id,size_mm,margin_mm,label);
    else if(extension==".png") write_marker_png(output,c,id,size_mm,margin_mm,dpi,label);
    else throw std::runtime_error("gen-marker output must end with .svg or .png");
    std::cout<<"marker_id="<<id<<", size="<<size_mm<<" mm, dpi="<<dpi<<", format="<<extension<<"\n";
    return 0;
}

int calibrate(int argc,char** argv) {
    const Config c=read_config(arg(argc,argv,"--config")); const std::string input=arg(argc,argv,"--input"), output=arg(argc,argv,"--output");
    if (input.empty()||output.empty()) throw std::runtime_error("calibrate requires --input and --output");
    const cv::Mat image=cv::imread(input); if (image.empty()) throw std::runtime_error("cannot read image: "+input);
    const double gate=arg(argc,argv,"--max-rmse-cm").empty()?2.0:to_double(arg(argc,argv,"--max-rmse-cm"),"max-rmse-cm");
    const DetectionResult d=calibrate_image(c,image); if (d.rmse_cm>gate) { std::cerr<<"RMSE gate failed: "<<d.rmse_cm<<" cm > "<<gate<<" cm\n"; return 2; }
    write_calibration(output,c,d,image.size(),arg(argc,argv,"--channel").empty()?-1:to_int(arg(argc,argv,"--channel"),"channel"),gate);
    std::cout<<"calibrated "<<d.ids.size()<<" markers, RMSE="<<d.rmse_cm<<" cm\n"; return 0;
}

json read_h(const std::string& path) { std::ifstream in(path); if(!in) throw std::runtime_error("cannot open homography: "+path); return json::parse(in); }

int view(int argc,char** argv) {
    const Config c=read_config(arg(argc,argv,"--config")); const std::string hp=arg(argc,argv,"--homography"), input=arg(argc,argv,"--input");
    if(hp.empty()||input.empty()) throw std::runtime_error("view requires --homography and --input");
    const json j=read_h(hp); const cv::Mat h=json_matrix(j.at("H_pixel_to_world")); const cv::Mat image=cv::imread(input); if(image.empty()) throw std::runtime_error("cannot read image");
    const int scale=20, width=static_cast<int>((c.cols*(c.marker_len_cm+c.gap_cm))*scale), height=static_cast<int>((c.rows*(c.marker_len_cm+c.gap_cm))*scale);
    cv::Mat world_scale = (cv::Mat_<double>(3,3) << 20,0,0, 0,20,0, 0,0,1);
    cv::Mat world_to_top = world_scale * h;
    cv::Mat top; cv::warpPerspective(image,top,world_to_top,cv::Size(width,height));
    cv::Mat errors=top.clone(); cv::Mat gray; cv::cvtColor(image,gray,cv::COLOR_BGR2GRAY); DetectionResult d=calibrate_image(c,gray);
    double sum = 0.0;
    for (size_t i=0; i<d.pixels.size(); ++i) {
        std::vector<cv::Point2f> projected;
        cv::perspectiveTransform(std::vector<cv::Point2f>{d.pixels[i]}, projected, h);
        const double dx=projected[0].x-d.worlds[i].x, dy=projected[0].y-d.worlds[i].y;
        const double error_cm=std::sqrt(dx*dx+dy*dy); sum += error_cm*error_cm;
        const cv::Scalar color = error_cm < 0.5 ? cv::Scalar(0,200,0) :
            (error_cm < 1.0 ? cv::Scalar(0,220,220) : cv::Scalar(0,0,255));
        std::vector<cv::Point2f> screen;
        cv::perspectiveTransform(std::vector<cv::Point2f>{d.worlds[i]}, screen, world_scale);
        cv::circle(errors, screen[0], 5, color, -1);
    }
    const int pitch_px=static_cast<int>((c.marker_len_cm+c.gap_cm)*20);
    for(int col=0;col<=c.cols;++col) cv::line(errors,{col*pitch_px,0},{col*pitch_px,height-1},cv::Scalar(0,180,0),1);
    for(int row=0;row<=c.rows;++row) cv::line(errors,{0,row*pitch_px},{width-1,row*pitch_px},cv::Scalar(0,180,0),1);
    std::string dir=arg(argc,argv,"--output-dir"); if(!dir.empty()){fs::create_directories(dir);cv::imwrite((fs::path(dir)/"point-error.png").string(),errors);cv::imwrite((fs::path(dir)/"topdown-warp.png").string(),top);}
    const double displayed_rmse = d.pixels.empty() ? 0.0 : std::sqrt(sum/d.pixels.size());
    cv::putText(errors, "RMSE=" + std::to_string(displayed_rmse) + " cm", {10,25},
        cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(20,20,20), 2);
    std::cout<<"RMSE="<<displayed_rmse<<" cm\n"; if(has_arg(argc,argv,"--live")){cv::imshow("point error",errors);cv::imshow("top-down warp",top);cv::waitKey(0);} return 0;
}

int selftest(bool verbose) {
    Config c; c.dictionary="DICT_4X4_50"; c.cols=4;c.rows=3;c.marker_len_cm=4;c.gap_cm=2;c.id_offset=10;
    const cv::Mat board=render_board(c,20); std::vector<cv::Point2f> src{{0,0},{static_cast<float>(board.cols),0},{static_cast<float>(board.cols),static_cast<float>(board.rows)},{0,static_cast<float>(board.rows)}};
    std::vector<cv::Point2f> dst{{80,60},{700,35},{735,540},{55,565}}; cv::Mat known=cv::getPerspectiveTransform(dst,src), camera(620,800,CV_8UC1,cv::Scalar(255));
    cv::Mat warped; cv::warpPerspective(board,warped,known,camera.size()); DetectionResult d=calibrate_image(c,warped);
    if(d.rmse_cm>0.25) throw std::runtime_error("selftest RMSE too high: "+std::to_string(d.rmse_cm));
    if(verbose) std::cout<<"selftest: markers="<<d.ids.size()<<", inliers="<<d.inliers<<", RMSE="<<d.rmse_cm<<" cm\n";
    return 0;
}

} // namespace

int main(int argc,char** argv) {
    if(argc<2){usage();return 1;} try { const std::string cmd=argv[1]; if(cmd=="gen-board")return gen_board(argc,argv); if(cmd=="gen-marker")return gen_marker(argc,argv); if(cmd=="calibrate")return calibrate(argc,argv); if(cmd=="solve-manual")return solve_manual(argc,argv); if(cmd=="view")return view(argc,argv); if(cmd=="selftest")return selftest(has_arg(argc,argv,"--verbose")); usage(); return 1; }
    catch(const std::exception& e){std::cerr<<"homography_tool: "<<e.what()<<'\n';return 1;}
}
