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

# EngineSDK 根目录。解析顺序：
#   1) -DENGINE_SDK=<路径>            显式指定（CACHE 变量，命令行优先级最高）
#   2) 默认 "D:/EngineSDK"            ⚠ 这是**作者机器上的路径**
#   3) 环境变量 HYBRIDENGINE_SDK      仅当上面那个默认值在本机不存在时采用；四个模板共用此变量名
set(ENGINE_SDK "D:/EngineSDK" CACHE PATH "HybridEngine EngineSDK root")
if(NOT EXISTS "${ENGINE_SDK}/include/hybridengine/bind/ms_bind.h" AND DEFINED ENV{HYBRIDENGINE_SDK})
  set(ENGINE_SDK "$ENV{HYBRIDENGINE_SDK}" CACHE PATH "HybridEngine EngineSDK root" FORCE)
endif()

# 尽早校验：缺 SDK 时给一句人话，而不是让 ninja 在链接阶段报
#   "ninja: error: 'D:/.../libhybridengine_bind.a' ... missing and no known rule to make it"
# ——配置阶段会成功，错误出现在下一步且完全不提 ENGINE_SDK（实测踩过这个坑）。
if(NOT EXISTS "${ENGINE_SDK}/include/hybridengine/bind/ms_bind.h")
  message(FATAL_ERROR
    "SDK 里找不到公共头：${ENGINE_SDK}/include/hybridengine/bind/ms_bind.h\n"
    "  当前 ENGINE_SDK = ${ENGINE_SDK}\n"
    "  请用以下任一方式指向 EngineSDK 根目录：\n"
    "    · cmake -S . -B build -DENGINE_SDK=<EngineSDK 根>\n"
    "    · 设环境变量 HYBRIDENGINE_SDK=<EngineSDK 根>\n"
    "    · 直接改本文件顶部的默认值")
endif()

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
