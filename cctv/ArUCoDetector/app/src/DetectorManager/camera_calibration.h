#pragma once

#include <opencv2/core.hpp>
#include <string>

struct CameraCalibration {
    bool valid = false;     // 로딩이 성공했는지를 표시하는 플래그
    
    // 이미지 정보는 JSON 파일에서 그대로 읽어옴
    // {"image_width":2592, "image_height":1520, ...}
    int image_width = 0;    
    int image_height = 0;   

    // 이 카메라 렌즈가 어디를 중심으로, 얼마나 확대해서 찍는지를 나타내는 값. (3x3 행렬)
    cv::Mat camera_matrix;  // 3x3, CV_64F

    // 렌즈 때문에 사진 가장자리로 갈수록 직선이 살짝 휘어 보이는 정도 (보정용 계수 5개)
    cv::Mat dist_coeffs;    // 1xN, CV_64F
};