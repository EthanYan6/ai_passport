#include "pcmon_link.h"

#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcmon_protocol.h"

static const char *TAG = "pcmon_link";

#define PCMON_LINK_LINE PCMON_LINE_MAX
#define PCMON_READ_CHUNK 64

static pcmon_link_cb_t s_cb;
static void *s_user;

static void link_emit_line(char *line, size_t *len)
{
    if (*len == 0) {
        return;
    }
    line[*len] = '\0';
    if (s_cb) {
        s_cb(line, s_user);
    }
    *len = 0;
}

static void link_reader_task(void *arg)
{
    (void)arg;
    char buf[PCMON_READ_CHUNK];
    char line[PCMON_LINK_LINE];
    size_t len = 0;

    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        for (ssize_t i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n' || ch == '\r') {
                link_emit_line(line, &len);
            } else if (len < sizeof(line) - 1) {
                line[len++] = ch;
            } else {
                len = 0;
            }
        }
    }
}

bool pcmon_link_init(pcmon_link_cb_t cb, void *user)
{
    s_cb = cb;
    s_user = user;
    BaseType_t ok = xTaskCreate(link_reader_task, "pcmon_link", 4096, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "reader task create failed");
        return false;
    }
    return true;
}

void pcmon_link_send(const char *frame)
{
    if (!frame) {
        return;
    }
    size_t len = strlen(frame);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(STDOUT_FILENO, frame + off, len - off);
        if (w <= 0) {
            break;
        }
        off += (size_t)w;
    }
}
