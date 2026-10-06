# 极简打卡固件 —— 顶层 CMake
# 引用官方仓库的 bsp 组件（显示/按键/电池），其余逻辑全部在本目录内。
cmake_minimum_required(VERSION 3.16)

# 把官方仓库 components 加入组件搜索路径（bsp 及其依赖）
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../ai-passport-repo/components")

set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(minimal-checkin)
