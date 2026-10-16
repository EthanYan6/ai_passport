cmake_minimum_required(VERSION 3.16)

# 引用共享 BSP（显示 / 电量 / 按键 / LVGL 初始化）
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../ai-passport-repo/components")

set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pc-monitor)
