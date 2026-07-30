#include "camera_calibration.h"

#include <fstream>
#include <sstream>

#include "json_utility.h"

// calib_result_ch{채널}.json 형태
// {
//   "rms": 0.4656808248030791,
//   "image_width": 2592,
//   "image_height": 1520,
//   "camera_matrix": [2915.57, 0.0, 1487.90, 0.0, 2908.93, 768.97, 0.0, 0.0, 1.0], // 3x3, row-major(행 우선) 9개
//   "dist_coeffs": [-0.5108, 0.5149, -0.01013, -0.00953, -1.1899],                 // k1,k2,p1,p2,k3 5개
//   "per_view_errors_px": [...]
// }
// 이 중 image_width/image_height, camera_matrix, dist_coeffs만 쓰고 rms, per_view_errors_px는 무시함.

// path는 ArUCoCalibration이 만든 calib_result_ch{채널}.json의 절대경로.
CameraCalibration LoadCameraCalibration(const std::string& path) {

    CameraCalibration result;

    std::ifstream ifs(path);
    if(!ifs.is_open()) {
        return result;
    }

    std::stringstream ss;
    ss << ifs.rdbuf();

    JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
    doc.Parse(ss.str());
    if (doc.HasParseError() || !doc.HasMember("camera_matrix") || !doc.HasMember("dist_coeffs")) {
        return result;
    }

    if (doc.HasMember("image_width") && doc.HasMember("image_height")) {
        result.image_width = doc["image_width"].GetInt();
        result.image_height = doc["image_height"].GetInt(); 
    }

    result.camera_matrix = cv::Mat(3, 3, CV_64F);
    int k = 0;
    for (auto& v : doc["camera_matrix"].GetArray()) {
        result.camera_matrix.at<double>(k / 3, k % 3) = v.GetDouble();
        ++k;
    }

    auto dc_array = doc["dist_coeffs"].GetArray();
    result.dist_coeffs = cv::Mat(1, (int)dc_array.Size(), CV_64F);
    int dc_i = 0;
    for (auto& v : dc_array) {
        result.dist_coeffs.at<double>(0, dc_i++) = v.GetDouble();
    }

    result.valid = true;    // JSON 파일을 잘 불러왔으면 성공
    return result;
}