set(TARGET_LIB detector_manager)

add_library(${TARGET_LIB} MODULE
  detector_manager.cc
  features/detection/aruco_detector.cc
  features/detection/detection_pipeline.cc
  features/detection/detection_dispatcher.cc
  features/detection/runtime_info.cc
  features/test/test_run_controller.cc
  features/metadata/metadata_xml_builder.cc
  features/settings/detection_settings.cc)

target_include_directories(${TARGET_LIB} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/component/includes
  ${CMAKE_CURRENT_SOURCE_DIR}/features/detection
  ${CMAKE_CURRENT_SOURCE_DIR}/features/metadata
  ${CMAKE_CURRENT_SOURCE_DIR}/features/settings
  ${WISENET_PRODUCT_RELEASE_PATH}/3rd_party/include
  ${WISENET_PRODUCT_RELEASE_PATH}/3rd_party/include/opencv4
  ${WISENET_PRODUCT_RELEASE_PATH}/includes/metadata
  ${WISENET_PRODUCT_RELEASE_PATH}/lib/npu_api/includes            # raw_image.h (RawImage)
  ${WISENET_PRODUCT_RELEASE_PATH}/includes/shared_buffer/video/includes
  ${WISENET_PRODUCT_RELEASE_PATH}/lib/cv5_sdk/video)
target_link_directories(${TARGET_LIB} PRIVATE ${CMAKE_HOME_DIRECTORY}/libs ${WISENET_PLATFORM_RELEASE_PATH}/lib ${WISENET_PRODUCT_RELEASE_PATH}/lib)

# 이전 빌드가 복사한 NPU 런타임은 동적 컴포넌트 스캔 대상이 되므로, 재빌드 때 잔여 파일을 제거한다.
file(REMOVE "${CMAKE_HOME_DIRECTORY}/libs/libnpu_api.so")

target_link_libraries(${TARGET_LIB} PRIVATE opencv_aruco opencv_calib3d opencv_features2d opencv_flann opencv_imgcodecs opencv_imgproc opencv_core libjpeg-turbo libpng libjasper zlib ittnotify quirc)
target_link_libraries(${TARGET_LIB} PRIVATE metadata_manager stdlib pthread)
# raw 비디오(SPMgrVideoRaw) 프레임 처리 라이브러리 (cv5) — IPLVideoFrameRaw/RawImage 심볼
target_link_libraries(${TARGET_LIB} PRIVATE sdk_cv5_video cv5_technical_streamingService_sharedBuffer)

install (TARGETS ${TARGET_LIB} DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB})
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/component/manifests/ DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB} FILES_MATCHING PATTERN "*manifest.json")
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/component/manifests/ DESTINATION ${CMAKE_HOME_DIRECTORY}/res/models FILES_MATCHING PATTERN "*instance*.json" PATTERN "*attribute*.json")
