set(TARGET_LIB detector_manager)

add_library(${TARGET_LIB} MODULE 
  detector_manager.cc
  camera/camera_credentials.cc
  camera/camera_snapshot_client.cc
  camera/camera_calibration.cc
  pipeline/aruco_detector.cc
  pipeline/frame_source.cc
  pipeline/frame_preprocessor.cc
  metadata/metadata_format.cc
  settings/detection_settings.cc
  debug/debug_detect_handler.cc)

target_include_directories(${TARGET_LIB} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/includes
  ${CMAKE_CURRENT_SOURCE_DIR}/camera
  ${CMAKE_CURRENT_SOURCE_DIR}/pipeline
  ${CMAKE_CURRENT_SOURCE_DIR}/metadata
  ${CMAKE_CURRENT_SOURCE_DIR}/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/debug
  ${WISENET_PRODUCT_RELEASE_PATH}/3rd_party/include
  ${WISENET_PRODUCT_RELEASE_PATH}/3rd_party/include/opencv4
  ${WISENET_PRODUCT_RELEASE_PATH}/includes/metadata)
target_link_directories(${TARGET_LIB} PRIVATE ${CMAKE_HOME_DIRECTORY}/libs ${WISENET_PLATFORM_RELEASE_PATH}/lib)

target_link_libraries(${TARGET_LIB} PRIVATE opencv_aruco opencv_calib3d opencv_features2d opencv_flann opencv_imgcodecs opencv_imgproc opencv_core libjpeg-turbo libpng libjasper zlib ittnotify quirc curl)
target_link_libraries(${TARGET_LIB} PRIVATE metadata_manager stdlib pthread)

install (TARGETS ${TARGET_LIB} DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB})
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifests/ DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB} FILES_MATCHING PATTERN "*manifest.json")
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifests/ DESTINATION ${CMAKE_HOME_DIRECTORY}/res/models FILES_MATCHING PATTERN "*instance*.json" PATTERN "*attribute*.json")
