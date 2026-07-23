set(TARGET_LIB sample_component)

include_directories(${WISENET_PRODUCT_RELEASE_PATH}/includes/metadata)

link_directories(${WISENET_PLATFORM_RELEASE_PATH}/lib)
link_libraries(metadata_manager)

add_library(${TARGET_LIB} MODULE sample_component.cc metadata_format.cc)

target_include_directories(${TARGET_LIB} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/includes)

target_link_directories(${TARGET_LIB} PRIVATE ${CMAKE_HOME_DIRECTORY}/libs)

target_link_libraries(${TARGET_LIB} PRIVATE stdlib)

install (TARGETS ${TARGET_LIB} DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB})
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifests/ DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB} FILES_MATCHING PATTERN "*manifest.json")
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifests/ DESTINATION ${CMAKE_HOME_DIRECTORY}/res/models FILES_MATCHING PATTERN "*instance*.json" PATTERN "*attribute*.json")
