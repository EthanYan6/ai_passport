# main 组件构建脚本
set(SRCS
    "main.c"
    "app_pcmon.c"
    "pcmon_protocol.c"
    "pcmon_link.c"
    # 中文 UI 字体子集（lv_font_conv 生成）
    "assets/fonts/app_font_14.c"
    "assets/fonts/app_font_20.c"
    "assets/fonts/app_font_40.c"
)

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES bsp esp_timer)

# lv_font_conv 生成的字体 .c 依据 LV_LVGL_H_INCLUDE_SIMPLE 选择 #include "lvgl.h"
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
