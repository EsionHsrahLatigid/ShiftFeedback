cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(ShiftFeedback VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(ShiftFeedback VERSION 0.1.0 LANGUAGES C CXX)
endif()

option(SHIFTFEEDBACK_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(SHIFTFEEDBACK_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(SHIFTFEEDBACK_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(SHIFTFEEDBACK_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(shiftfeedback_engine STATIC
    source/violent/ShiftFeedbackEngine.cpp)
target_include_directories(shiftfeedback_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(shiftfeedback_engine PUBLIC cxx_std_20)
set_target_properties(shiftfeedback_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shiftfeedback_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SHIFTFEEDBACK_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(shiftfeedback_engine_tests
        tests/ShiftFeedbackEngineTests.cpp)
    target_link_libraries(shiftfeedback_engine_tests PRIVATE shiftfeedback_engine)
    target_compile_features(shiftfeedback_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(shiftfeedback_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(shiftfeedback_engine_tests)
    add_test(NAME shiftfeedback_engine_tests COMMAND shiftfeedback_engine_tests)
endif()

if(SHIFTFEEDBACK_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${SHIFTFEEDBACK_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${SHIFTFEEDBACK_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
    else()
        include(FetchContent)
        FetchContent_Declare(yup
            GIT_REPOSITORY https://github.com/kunitoki/yup.git
            GIT_TAG 9a1c9bc699b6a714f6f52486462d98a140c8bf95)
        FetchContent_MakeAvailable(yup)
    endif()

    # YUP's iconutil-based helper rejects its generated iconset on current
    # macOS. Keep the compatibility fix project-local until upstream changes.
    if(APPLE)
        include(cmake/YupMacOSIconWorkaround.cmake)
    endif()

    set(SHIFTFEEDBACK_CREATE_AU OFF)
    if(APPLE)
        set(SHIFTFEEDBACK_CREATE_AU ON)
    endif()

    yup_audio_plugin(
        TARGET_NAME shiftfeedback
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "ShiftFeedback"
        TARGET_APP_ID "audio.2bit.shiftfeedback"
        TARGET_APP_NAMESPACE "audio.2bit"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "audio.2bit.ShiftFeedback"
        PLUGIN_NAME "ShiftFeedback"
        PLUGIN_VENDOR "2bit"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "Fixed-Hz frequency-shifted cross-feedback cloud"
        PLUGIN_URL "https://github.com/2bbb/ShiftFeedback"
        PLUGIN_IS_SYNTH ON
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${SHIFTFEEDBACK_CREATE_AU}
        PLUGIN_AU_SUBTYPE "SfBk"
        PLUGIN_AU_MANUFACTURER "2Bit"
        PLUGIN_AU_SANDBOX_SAFE ON
        PLUGIN_CREATE_AUv3 OFF
        PLUGIN_CREATE_LV2 OFF
        PLUGIN_CREATE_AAX OFF
        PLUGIN_CREATE_STANDALONE ON
        PLUGIN_COPY_AFTER_BUILD OFF
        MODULES
            yup::yup_gui
            yup::yup_audio_processors)

    target_sources(shiftfeedback_shared INTERFACE
        source/violent/ParameterGridEditor.cpp
        source/violent/ParameterGridEditor.h
        source/violent/ProductState.h
        source/violent/plugins/ShiftFeedbackPlugin.cpp
        source/violent/plugins/ShiftFeedbackPlugin.h)
    target_link_libraries(shiftfeedback_shared INTERFACE shiftfeedback_engine)
    target_include_directories(shiftfeedback_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    set(SHIFTFEEDBACK_BUNDLE_TARGETS
        shiftfeedback_standalone_plugin
        shiftfeedback_vst3_plugin)
    if(APPLE)
        list(APPEND SHIFTFEEDBACK_BUNDLE_TARGETS shiftfeedback_au_plugin)
    endif()

    add_custom_target(shiftfeedback_bundles
        DEPENDS ${SHIFTFEEDBACK_BUNDLE_TARGETS})

    if(APPLE AND SHIFTFEEDBACK_ADHOC_SIGN)
        find_program(SHIFTFEEDBACK_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET shiftfeedback_vst3_plugin POST_BUILD
            COMMAND "${SHIFTFEEDBACK_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:shiftfeedback_vst3_plugin>"
            COMMENT "Ad-hoc signing ShiftFeedback VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET shiftfeedback_standalone_plugin POST_BUILD
            COMMAND "${SHIFTFEEDBACK_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:shiftfeedback_standalone_plugin>"
            COMMENT "Ad-hoc signing ShiftFeedback standalone app for local launch"
            VERBATIM)
    endif()
endif()
