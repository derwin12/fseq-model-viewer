# fseq-model-viewer: pick a show folder, an .fseq and a model, and play that
# model's channels from the .fseq in a preview window.
#
# Links the wx-free xLights core (xlights_core.lib) built by the xLights repo's
# full-core unit-test build, so models, start channels and node layout are
# exactly what xLights computes. Release only: the core lib is /MD Release.
#
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#   cmake --build build --config Release
#   build\Release\FseqModelViewer.exe
cmake_minimum_required(VERSION 3.20)
project(FseqModelViewer CXX C)

set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(XLIGHTS_SRC "H:/XlightsSourceDir/xlights" CACHE PATH "xLights source checkout")
set(XLIGHTS_CORE_LIB "${XLIGHTS_SRC}/build-tests-full/tests/unit/Release/xlights_core.lib"
    CACHE FILEPATH "xlights_core.lib from the xLights full-core unit-test build")
set(XL_BUNDLE "${XLIGHTS_SRC}/dependencies-bundle")

if(NOT EXISTS "${XLIGHTS_CORE_LIB}")
    message(FATAL_ERROR "xlights_core.lib not found at ${XLIGHTS_CORE_LIB}. Build it in the xLights repo:\n"
        "  cmake -S . -B build-tests-full -G \"Visual Studio 17 2022\" -DXL_BUILD_TESTS=ON -DXL_TEST_FULL_CORE=ON\n"
        "  cmake --build build-tests-full --config Release --target xlights_core")
endif()

set(wxWidgets_ROOT_DIR "H:/XlightsSourceDir/wxWidgets" CACHE PATH "wxWidgets root")
set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/vc_x64_lib" CACHE PATH "wxWidgets lib dir")
set(wxWidgets_CONFIGURATION mswu)
find_package(wxWidgets 3.3 REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(FseqModelViewer WIN32
    src/main.cpp
    src/app.rc
    # The core's shader code expects these GL entry points from the UI layer.
    ${XLIGHTS_SRC}/tests/unit/support/GLFunctionPointers.cpp)

target_compile_definitions(FseqModelViewer PRIVATE
    NOMINMAX _CRT_SECURE_NO_WARNINGS XLIGHTS_CMAKE_BUILD IGNORE_VENDORS UNICODE _UNICODE)
target_compile_options(FseqModelViewer PRIVATE /utf-8 /MP /Zi)
target_link_options(FseqModelViewer PRIVATE /DEBUG /OPT:REF /OPT:ICF)

target_include_directories(FseqModelViewer PRIVATE
    ${XLIGHTS_SRC}/src-core
    ${XLIGHTS_SRC}/src-core/render
    ${XLIGHTS_SRC}/src-core/utils
    ${XLIGHTS_SRC}/dependencies
    ${XLIGHTS_SRC}/dependencies/spdlog/include
    ${XLIGHTS_SRC}/dependencies/pugixml/src
    ${XLIGHTS_SRC}/include
    ${XL_BUNDLE}/include
    ${XLIGHTS_SRC}/include/sol2-3.5.0
    ${XL_BUNDLE}/include/SDL2)

target_link_directories(FseqModelViewer PRIVATE ${XL_BUNDLE}/lib ${XLIGHTS_SRC}/lib/windows64)
target_link_libraries(FseqModelViewer PRIVATE
    ${XLIGHTS_CORE_LIB}
    ${wxWidgets_LIBRARIES}
    avcodec avformat avutil avfilter swscale swresample
    SDL2 libzstd_static_VS lua minizip z libcurl xlsxwriter liquidfun
    opengl32 glu32 ws2_32 winmm comctl32 rpcrt4 mfuuid propsys wmcodecdspuuid iphlpapi)
target_link_options(FseqModelViewer PRIVATE /NODEFAULTLIB:LIBCMT)

# Runtime DLLs the core needs (FFmpeg, SDL2, curl).
foreach(pat "av*.dll" "sw*.dll" "SDL2.dll" "libcurl.dll")
    add_custom_command(TARGET FseqModelViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DSRC_DIR=${XLIGHTS_SRC}/bin64" "-DDST_DIR=$<TARGET_FILE_DIR:FseqModelViewer>"
                "-DGLOB_PAT=${pat}" -P "${XLIGHTS_SRC}/cmake/safe_copy_dlls.cmake")
endforeach()
