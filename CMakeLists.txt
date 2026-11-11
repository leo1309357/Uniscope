cmake_minimum_required(VERSION 3.15)

project(uniscope-2d
    VERSION 0.1.0
    DESCRIPTION "Uniscope 2D graphics library (OpenGL 3.3)"
    LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(UNISCOPE2D_GFX_SOURCES
    src/gfx/Renderer2D.cpp
    src/gfx/ShaderProgram.cpp
    src/gfx/ShaderLibrary.cpp
    src/gfx/Texture2D.cpp
    src/gfx/Texture2DArray.cpp
    src/gfx/SdfFont.cpp
    src/gfx/FontAssets.cpp
    src/gfx/FontRegistry.cpp
    src/gfx/FontPlatformPaths.cpp
    src/gfx/Context2D.cpp
    src/gfx/GlCompat.cpp
    src/gfx/AndroidAssetReader.cpp
)

set(UNISCOPE2D_CORE_SOURCES
    src/core/AssetPaths.cpp
    src/platform/GlSurface.cpp
    src/pal/PlatformFactory.cpp
    src/pal/common/StdClock.cpp
    src/pal/common/StdLogSink.cpp
    src/pal/common/FileSystemAssetSource.cpp
    src/pal/common/StdPathProvider.cpp
    src/Device.cpp
    src/render/RenderTarget.cpp
    src/render/RenderPass.cpp
    src/lighting/ShadowCaster.cpp
    src/anomaly/AnomalyField.cpp
    src/anomaly/AnomalyGrade.cpp
    src/animation/Skeleton.cpp
    src/animation/AnimationClip.cpp
    src/animation/AnimationPlayer.cpp
    src/animation/Material2D.cpp
    src/animation/Rig2D.cpp
    src/animation/RigProportions2D.cpp
    src/animation/RigSkeletonBuilder2D.cpp
    src/animation/RigGeometry2D.cpp
    src/animation/RigOverlay2D.cpp
    src/animation/RigClipSet2D.cpp
    src/animation/RigAtlas2D.cpp
    src/animation/RigAttachment2D.cpp
    src/animation/SecondaryChain2D.cpp
    src/animation/Timeline.cpp
    src/VisualFeatureRegistry.cpp
)

if(ANDROID)
    list(APPEND UNISCOPE2D_CORE_SOURCES src/pal/android/AndroidPlatform.cpp)
else()
    list(APPEND UNISCOPE2D_CORE_SOURCES src/pal/glfw/GlfwPlatform.cpp)
endif()

add_library(uniscope-2d STATIC
    ${UNISCOPE2D_GFX_SOURCES}
    ${UNISCOPE2D_CORE_SOURCES}
)
add_library(Reverseworks::Uniscope2D ALIAS uniscope-2d)

target_include_directories(uniscope-2d
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lighting
        ${CMAKE_CURRENT_SOURCE_DIR}/src/animation
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)

target_link_libraries(uniscope-2d
    PUBLIC glad
    PRIVATE stb_image Threads::Threads
)

target_include_directories(uniscope-2d
    PRIVATE
        ${CMAKE_SOURCE_DIR}/3rdParty/stb/include
        ${CMAKE_SOURCE_DIR}/3rdParty/stb/include/stb
)

if(ANDROID)
    # android_native_app_glue.h (android_app lifecycle struct) lives in the
    # NDK's native_app_glue sources; the glue .c is compiled into the app's
    # shared library, so the engine only needs the header here.
    target_include_directories(uniscope-2d PRIVATE
        ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue
    )
    target_link_libraries(uniscope-2d PUBLIC EGL GLESv3 android log)
elseif(TARGET glfw)
    target_link_libraries(uniscope-2d PUBLIC glfw)
endif()

option(UNISCOPE2D_BUILD_TESTS "Build Uniscope-2D self-check executables" ON)

function(uniscope2d_copy_shaders target)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:${target}>/shaders"
            COMMENT "Copying Uniscope-2D shaders for ${target}"
        )
    endif()
endfunction()

if(UNISCOPE2D_BUILD_TESTS)
    add_executable(Uniscope2D-Smoke tests/SmokeTest.cpp)
    target_link_libraries(Uniscope2D-Smoke PRIVATE uniscope-2d)
    uniscope2d_copy_shaders(Uniscope2D-Smoke)

    add_executable(Uniscope2D-Anomaly tests/AnomalyTest.cpp)
    target_link_libraries(Uniscope2D-Anomaly PRIVATE uniscope-2d)
endif()

install(TARGETS uniscope-2d
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
