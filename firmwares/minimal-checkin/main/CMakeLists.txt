# main 组件构建脚本
set(SRCS
    "main.c"
    "checkin_logic.c"
    "checkin_store.c"
    "wifi_manager.c"
    "portal_http.c"
    "time_sync.c"
    "holiday.c"
    "ui_theme.c"
    "ui_main.c"
    "ui_picker.c"
    "ui_calendar.c"
    "ui_detail.c"
    "ui_provision.c"
    "ui_standby.c"
    "third_party/qrcodegen.c"
    "assets/fonts/font_cn_16.c"
    "assets/fonts/font_cn_24.c"
    "assets/fonts/font_day_96.c"
)

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "." "third_party"
                       REQUIRES bsp esp_wifi esp_netif esp_event nvs_flash json lwip esp_timer)

# lv_font_conv 生成的字体 .c 依据 LV_LVGL_H_INCLUDE_SIMPLE 选择 #include "lvgl.h"
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# 主机侧纯逻辑测试（不参与固件构建，用 host 编译器手动跑）
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # 测试文件不注册进固件，见 tests/README 说明
endif()
