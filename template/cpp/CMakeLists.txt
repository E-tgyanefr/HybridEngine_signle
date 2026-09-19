# ============================================================================
# HybridEngine EngineSDK — C++ 消费模板的 CMake 接线（链接静态 libhybridengine_bind）
#
# 接线要点：
#   - 头文件：EngineSDK\include（ms_bind.h——唯一需要 include 的引擎头）
#   - 静态库：EngineSDK\lib\libhybridengine_bind.a（bind 层——内部链 app/core/platform/render3d 全部依赖）
#   - 系统库：user32 gdi32 winmm（Windows 平台 SDK——引擎零第三方红线的系统依赖）
#   - 运行：exe 旁需 EngineSDK\bin\hybridengine.dll + mingw 运行时（libgcc_s_seh-1/libstdc++-6/libwinpthread-1）
#     ——静态库消费时 DLL 仅作 GDI/输入运行时共生件；最简单：把 EngineSDK\bin 加入 PATH。
# ============================================================================
cmake_minimum_required(VERSION 3.24)
project(hybridengine_sdk_template_cpp LANGUAGES CXX)

# EngineSDK 根目录（默认 D:/EngineSDK；你的项目可改这里或用 -DENGINE_SDK=... 覆盖）
set(ENGINE_SDK "D:/EngineSDK" CACHE PATH "HybridEngine EngineSDK root")

add_executable(template_cpp main.cpp)
target_include_directories(template_cpp PRIVATE "${ENGINE_SDK}/include")
# 静态库全链（bind → app/render3d → core/platform/plugin：--start-group 循环引用安全——直接给 .a 时
# CMake 的传递依赖不会自动展开，必须显式链接全部六个 + 系统库；d3d11/dxgi/d3dcompiler=Render3D GpuRenderer
# 依赖（Windows 系统组件——零第三方红线）；plugin=ms_engine_create 的 Plugin::LoadAll 符号）
target_link_libraries(template_cpp PRIVATE
    "-Wl,--start-group"
    "${ENGINE_SDK}/lib/libhybridengine_bind.a"
    "${ENGINE_SDK}/lib/libhybridengine_app.a"
    "${ENGINE_SDK}/lib/libhybridengine_render3d.a"
    "${ENGINE_SDK}/lib/libhybridengine_core.a"
    "${ENGINE_SDK}/lib/libhybridengine_platform.a"
    "${ENGINE_SDK}/lib/libhybridengine_plugin.a"
    "-Wl,--end-group"
    user32 gdi32 winmm d3d11 dxgi d3dcompiler
)
# C++23（2026-09-17：原先写死 -std=c++17，与引擎/其它模板不一致）。
#   本模板只 include ms_bind.h（纯 C ABI 头），理论上更低标准也能编，但**统一到 23** 避免
#   "模板间标准不一致"这种隐性坑（引擎公共头在 native 模板里是 C++23 要求）。
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
target_compile_options(template_cpp PRIVATE -Wall -Wextra)

# 运行提示：把 ${ENGINE_SDK}/bin 加入 PATH（或复制 hybridengine.dll + mingw 运行时到输出目录）
set_target_properties(template_cpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app"
)
