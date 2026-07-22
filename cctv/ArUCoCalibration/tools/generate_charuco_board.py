#!/usr/bin/env python3
"""ChArUco 보드 이미지 생성기 - 카메라 캘리브레이션용 실물 인쇄본

사용법:
    pip install opencv-contrib-python pillow
    python generate_charuco_board.py [squares_x] [squares_y] [square_mm] [marker_mm]
    (인자 생략 시 기본값: 7 5 40 20  ->  ArUCo_calibration 앱의 /board 기본값과 동일)

인쇄 시 반드시 "실제 크기(100%)"로 인쇄해야 사각형이 정확히 지정한 mm가 됩니다
(이미지에 300dpi 정보가 박혀있어서, 뷰어/프린터가 그 값을 지켜주는 경우
"자동/맞춤 인쇄"가 아니라 "실제 크기"를 선택해야 함).

인쇄 후에는 반드시 자로 사각형 한 칸을 실측해서, 실제 나온 mm 값을
ArUCo_calibration 앱의 /board 설정에 다시 넣어야 합니다 (프린터 스케일링 오차 보정).
"""
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")  # Windows 콘솔 codepage 때문에 한글이 깨지는 것 방지

import cv2
from cv2 import aruco
from PIL import Image

DICTIONARY = aruco.DICT_4X4_50   # 카메라 앱(ArUCo_calibration) 쪽과 반드시 같은 딕셔너리

SQUARES_X = int(sys.argv[1]) if len(sys.argv) > 1 else 7
SQUARES_Y = int(sys.argv[2]) if len(sys.argv) > 2 else 5
SQUARE_MM = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0
MARKER_MM = float(sys.argv[4]) if len(sys.argv) > 4 else 20.0

PRINT_DPI = 300        # 인쇄 해상도 (인치당 픽셀)
MARGIN_MM = 10.0       # 보드 바깥 흰 여백 (한쪽 기준)

OUTPUT_PATH = f"charuco_board_{SQUARES_X}x{SQUARES_Y}_{int(SQUARE_MM)}mm.png"


def make_board(dictionary, squares_x: int, squares_y: int, square_m: float, marker_m: float):
    if hasattr(aruco, "CharucoBoard_create"):
        # opencv-contrib-python < 4.7 (예: 3.4.x, 4.6.x)
        return aruco.CharucoBoard_create(squares_x, squares_y, square_m, marker_m, dictionary)
    else:
        # opencv-contrib-python >= 4.7. 신규 API는 기본값이 legacy와 다른 배치라서,
        # 구버전 cv::aruco::CharucoBoard::create를 쓰는 앱 코드와 맞추려면 반드시 legacy로 고정해야 함.
        board = aruco.CharucoBoard((squares_x, squares_y), square_m, marker_m, dictionary)
        board.setLegacyPattern(True)
        return board


def render_board(board, out_w: int, out_h: int, margin_px: int):
    if hasattr(board, "generateImage"):
        # opencv-contrib-python >= 4.7
        return board.generateImage((out_w, out_h), marginSize=margin_px, borderBits=1)
    else:
        # opencv-contrib-python < 4.7
        return board.draw((out_w, out_h), marginSize=margin_px, borderBits=1)


def main():
    dictionary = aruco.getPredefinedDictionary(DICTIONARY)
    # mm 단위 그대로 넣어도 계산상 문제없음 (calibrateCameraCharuco 쪽 단위와만 일관되면 됨.
    # ArUCo_calibration 앱은 내부적으로 m로 변환해서 쓰므로 여기서는 그냥 mm로 board를 만들고
    # 이미지 렌더링(px 환산)만 정확히 하면 됨).
    board = make_board(dictionary, SQUARES_X, SQUARES_Y, SQUARE_MM, MARKER_MM)

    px_per_mm = PRINT_DPI / 25.4
    board_w_mm = SQUARES_X * SQUARE_MM + 2 * MARGIN_MM
    board_h_mm = SQUARES_Y * SQUARE_MM + 2 * MARGIN_MM
    out_w = round(board_w_mm * px_per_mm)
    out_h = round(board_h_mm * px_per_mm)
    margin_px = round(MARGIN_MM * px_per_mm)

    img = render_board(board, out_w, out_h, margin_px)

    # DPI 메타데이터를 박아야 뷰어/프린터가 "실제 크기"를 알 수 있음 (cv2.imwrite는 이걸 못 넣음)
    Image.fromarray(img).save(OUTPUT_PATH, dpi=(PRINT_DPI, PRINT_DPI))

    print(f"생성 완료: {OUTPUT_PATH}")
    print(f"격자: {SQUARES_X}x{SQUARES_Y}, 사각형 {SQUARE_MM}mm, 마커 {MARKER_MM}mm, dictionary=DICT_4X4_50")
    print(f"이미지 크기: {out_w}x{out_h}px @ {PRINT_DPI}dpi = 인쇄 시 {board_w_mm:.1f}mm x {board_h_mm:.1f}mm (여백 {MARGIN_MM}mm 포함)")
    print("인쇄할 때 '자동 맞춤'이 아니라 '실제 크기(100%)'로 인쇄해야 정확한 크기가 나옵니다.")
    print("인쇄 후 자로 사각형 한 칸을 실측해서, 실제 값을 ArUCo_calibration 앱의 /board 설정에 다시 넣어주세요.")


if __name__ == "__main__":
    main()
