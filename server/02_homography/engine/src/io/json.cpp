#include "homography/json.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace homography {

json matrix_to_json(const cv::Mat& matrix) {
    // OpenCV 행렬을 사람이 확인하기 쉬운 [[a,b,c], [d,e,f], [g,h,i]]
    // 형태의 숫자 배열로 변환함.
    json result = json::array();
    for (int row = 0; row < matrix.rows; ++row) {
        json values = json::array();
        for (int col = 0; col < matrix.cols; ++col)
            values.push_back(matrix.at<double>(row, col));
        result.push_back(values);
    }
    return result;
}

cv::Mat json_to_matrix(const json& value) {
    // 결과 파일의 3x3 배열을 호모그래피 행렬로 복원함.
    // 행렬 크기와 숫자 형식이 맞지 않으면 JSON 라이브러리 오류 발생함.
    cv::Mat matrix(3, 3, CV_64F);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            matrix.at<double>(row, col) = value.at(row).at(col).get<double>();
    return matrix;
}

std::string utc_now() {
    // 장치의 지역 시간과 관계없이 결과 생성 시점을 비교하도록
    // UTC와 ISO-8601 형식으로 기록함.
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm time{};
    gmtime_r(&now, &time);
    std::ostringstream output;
    output << std::put_time(&time, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

json config_to_json(const Config& config) {
    // 캘리브레이션 결과에 당시 설정을 함께 넣어 같은 조건의
    // 결과 해석과 재현에 사용함.
    return {
        {"dictionary", config.dictionary}, {"cols", config.cols},
        {"rows", config.rows}, {"marker_len_cm", config.marker_len_cm},
        {"gap_cm", config.gap_cm}, {"id_offset", config.id_offset},
        {"origin_corner", config.origin_corner},
        {"marker_output", {
            {"size_mm", config.marker_output.size_mm},
            {"margin_mm", config.marker_output.margin_mm},
            {"dpi", config.marker_output.dpi},
            {"label", config.marker_output.label}}},
        {"calibration", {
            {"max_rmse_cm", config.calibration.max_rmse_cm},
            {"ransac_threshold_cm", config.calibration.ransac_threshold_cm},
            {"channel", config.calibration.channel}}},
        {"manual_solve", {
            {"marker_size_mm", config.manual_solve.marker_size_mm},
            {"ransac_threshold_mm", config.manual_solve.ransac_threshold_mm}}},
        {"preview", {
            {"scale", config.preview.scale},
            {"good_error_cm", config.preview.good_error_cm},
            {"warning_error_cm", config.preview.warning_error_cm}}}
    };
}

json read_homography(const std::string& path) {
    // view 명령이 사용할 결과 JSON을 읽음. 파일 열기와 JSON 파싱만
    // 담당하며, 행렬 형식 검사는 json_to_matrix에서 처리함.
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open homography: " + path);
    return json::parse(input);
}

}  // namespace homography
