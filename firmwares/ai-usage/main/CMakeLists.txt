# main 组件构建脚本
set(SRCS
    "main.c"
    "usage_store.c"
    "wifi_manager.c"
    "portal_http.c"
    "time_sync.c"
    "ark_client.c"
    "zcode_client.c"
    "srv_client.c"
    "usage_worker.c"
    "ui_common.c"
    "ui_ark.c"
    "ui_zcode.c"
    "ui_server.c"
    "ui_settings.c"
    "ui_provision.c"
    "third_party/qrcodegen.c"
    "assets/fonts/app_font_14.c"
    "assets/fonts/app_font_20.c"
    "assets/fonts/app_font_40.c"
)

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "." "third_party"
                       REQUIRES bsp esp_wifi esp_netif esp_event nvs_flash json lwip esp_timer esp_http_client esp-tls mbedtls wolfssh wolfssl)

# lv_font_conv 风格生成的字体 .c 依据 LV_LVGL_H_INCLUDE_SIMPLE 选择 #include "lvgl.h"
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
# wolfssh 头文件按 WOLFSSL_USER_SETTINGS 走 user_settings.h 分支
# （组件本体无 wolfssl/options.h；宏需与 wolfssh/wolfssl 组件编译时一致）
target_compile_definitions(${COMPONENT_LIB} PRIVATE
                          WOLFSSL_USER_SETTINGS
                          WOLFSSH_USER_SETTINGS)
