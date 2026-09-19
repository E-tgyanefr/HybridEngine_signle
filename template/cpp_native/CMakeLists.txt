# ============================================================================
# HybridEngine EngineSDK — **原生 C++** 消费模板（完整公共 API + 静态库）
#
# 与隔壁 cpp/（C ABI，ms_bind.h）的分工：
#   cpp/        = 稳定 C ABI 消费者（跨语言同构；也适合不想依赖 C++ ABI 的场景）
#   cpp_native/ = **原生 C++ 消费者**：直接 include 完整公共头，用引擎的 C++ API 写游戏
#                 （App::Engine / Core::Component / Platform::IRenderer / Editor 字体）
#
# 接线要点（也是"只拿 SDK 能不能构建"的检查清单）：
#   - 头：${ENGINE_SDK}/include           → 需要**完整公共头树**，不只是 ms_bind.h
#   - 库：${ENGINE_SDK}/lib/*.a          → 需要 **editor 库**（IRenderer 只有图元，
#                                          文本能力在 Editor::DrawUiTextPt）
#   - 链接：必须用 --start-group/--end-group 包住（引擎各静态库互相依赖，
#          ld 对归档是**单遍**扫描，扁平 lib/ 下顺序不保证 → 不加就会 undefined reference）
#   - 系统库：user32 gdi32 winmm + d3d11 dxgi d3dcompiler
#         （D3D11 三个是 render3d GpuRenderer 的依赖；引擎源码里那三个
#          `#pragma comment(lib,...)` 只对 MSVC 生效，MinGW 下必须这里显式给）
#   - 标准：C++20（引擎公共头用到 C++20 特性）
# ============================================================================
cmake_minimum_required(VERSION 3.24)
project(hybridengine_sdk_template_cpp_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)   # 与引擎一致（引擎 2026-09-17 起要求 C++23）
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE_SDK "D:/EngineSDK" CACHE PATH "HybridEngine EngineSDK root")
if(NOT EXISTS "${ENGINE_SDK}/include/hybridengine/app/engine.hpp")
    message(FATAL_ERROR "SDK 里找不到公共头：${ENGINE_SDK}/include/hybridengine/app/engine.hpp\n"
                        "需要一个含**完整公共头树**的 SDK（用引擎 scripts\\release.ps1 生成）。")
endif()

# 非 ASCII 路径警告：MinGW Makefiles 在**含中文/空格的路径**下必然失败
#   （mingw32-make 按本地代码页解码路径 → 报 "No rule to make target '.../程序/...'"，实测复现）。
#   Ninja 在同样路径下正常。这里提前给出可操作的提示，免得消费者对着假错误排查半天。
if(CMAKE_CURRENT_SOURCE_DIR MATCHES "[^A-Za-z0-9_./:\\\\-]")
    message(WARNING
        "工程路径含非 ASCII 字符：${CMAKE_CURRENT_SOURCE_DIR}\n"
        "  若使用 MinGW Makefiles 生成器，构建会失败（mingw32-make 解码路径乱码）。\n"
        "  请改用 Ninja：cmake -S . -B build -G Ninja -DENGINE_SDK=...")
endif()

add_executable(template_cpp_native main.cpp)
target_include_directories(template_cpp_native PRIVATE "${ENGINE_SDK}/include")
target_link_libraries(template_cpp_native PRIVATE
    "-Wl,--start-group"
    "${ENGINE_SDK}/lib/libhybridengine_app.a"
    "${ENGINE_SDK}/lib/libhybridengine_editor.a"
    "${ENGINE_SDK}/lib/libhybridengine_render3d.a"
    "${ENGINE_SDK}/lib/libhybridengine_platform.a"
    "${ENGINE_SDK}/lib/libhybridengine_core.a"
    "${ENGINE_SDK}/lib/libhybridengine_plugin.a"
    "-Wl,--end-group"
    user32 gdi32 winmm d3d11 dxgi d3dcompiler
)
target_compile_options(template_cpp_native PRIVATE -Wall -Wextra)
set_target_properties(template_cpp_native PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app"
)
