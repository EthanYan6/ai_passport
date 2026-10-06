// main/time_sync.c —— SNTP 实现
#include "time_sync.h"
#include "app_config.h"

#include "esp_netif_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "time_sync";
static bool s_started;

void time_sync_start(void)
{
    if (s_started) return;
    s_started = true;

    setenv("TZ", APP_TZ, 1);
    tzset();

    // DEFAULT_CONFIG 已内置：servers[0]=主服务器、ip_event_to_renew=IP_EVENT_STA_GOT_IP
    // （断线重连后自动重新对时）、start=true
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_SNTP_HOST_1);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
    ESP_LOGI(TAG, "SNTP 已启动，等待对时: %s", APP_SNTP_HOST_1);
}

bool time_sync_valid(void)
{
    return time(NULL) > APP_MIN_VALID_TIME;
}

int time_sync_today(int *year, int *month, int *day)
{
    if (!time_sync_valid()) return 0;
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (year) *year = lt.tm_year + 1900;
    if (month) *month = lt.tm_mon + 1;
    if (day) *day = lt.tm_mday;
    return lt.tm_mday;
}
