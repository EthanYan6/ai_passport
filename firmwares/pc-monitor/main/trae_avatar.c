// 10x10 pixel companion for the TRAE battle page. See trae_avatar.h.
//
// The sprite is rendered into a single lv_canvas backed by a static RGB565
// buffer (outside LVGL's pool -- a per-pixel lv_obj grid needs ~15KB of pool
// and exhausted it, crashing the first render). State switches and frame
// animation only rewrite the buffer, so calls from the render loop stay cheap.

#include "trae_avatar.h"

#include <stdbool.h>

#include "lvgl.h"
#include "pcmon_protocol.h"

#define AV_N 10
#define AV_STATES 6
#define AV_PX_MAX 10 // largest supported pixel scale (100px sprite)

// Sprite legend: '.' transparent (page background), 'b' body, 'd' dark detail,
// 'a' accent. s_frames[state][frame][row], state = pcmon_trae_state_t order.
static const char *const s_frames[AV_STATES][2][AV_N] = {
    // IDLE: knocked-out flat on the floor, Zzz drifting above.
    {
        {
            "......zz..",
            "..........",
            "..........",
            "..........",
            "..........",
            "..........",
            "..bbbb....",
            ".bdbbbb...",
            "..bbbb....",
            "..........",
        },
        {
            "..zz......",
            "..........",
            "..........",
            "..........",
            "..........",
            "..........",
            "..bbbb....",
            ".bdbbbb...",
            "..bbbb....",
            "..........",
        },
    },
    // THINK: speech bubble pulses above the head.
    {
        {
            "......aaa.",
            "......a...",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "...bbbb...",
            "..baaaab..",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
        },
        {
            ".....aaa..",
            "......a...",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "...bbbb...",
            "..baaaab..",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
        },
    },
    // CODE: hands typing, keyboard accent flickers.
    {
        {
            "..........",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "...bbbb...",
            ".bbaaaabb.",
            "..bbbbbb..",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
        },
        {
            "..........",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "...bbbb...",
            ".baaaaabb.",
            "..bbbbbb..",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
        },
    },
    // BUILD: spinner turns overhead.
    {
        {
            "....aa....",
            ".....a....",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "...bbbb...",
            "..baaaab..",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
        },
        {
            "....a.a...",
            "....a.....",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "...bbbb...",
            "..baaaab..",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
        },
    },
    // PASS: confetti bounces between the corners.
    {
        {
            "a........a",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "..baaaab..",
            ".b.bbbb.b.",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
            "..........",
        },
        {
            ".a......a.",
            "..bbbbbb..",
            "..bd..db..",
            "..bbbbbb..",
            "..baaaab..",
            ".b.bbbb.b.",
            "...bbbb...",
            "...b..b...",
            "...b..b...",
            "a........a",
        },
    },
    // FAIL: lying flat, twitching with a sweat drop.
    {
        {
            "..........",
            "..........",
            "..........",
            "..........",
            "..........",
            "..........",
            "..bbbb....",
            ".bdbbbb...",
            "..bbbb..a.",
            "..........",
        },
        {
            "..........",
            "..........",
            "..........",
            "..........",
            "..........",
            "..........",
            "..bbbb....",
            ".bdbbbb...",
            ".abbbb....",
            "..........",
        },
    },
};

static const uint32_t s_accent[AV_STATES] = {
    0x6E8CA0, // idle: muted Zzz over the knocked-out body
    0x2FE6FF, // think: cyan bubble
    0x39FF88, // code: green keyboard
    0xFFC24D, // build: amber spinner
    0x39FF88, // pass: green confetti
    0xFF4D6D, // fail: red sweat
};

#define AV_BODY 0xAFC3D1
#define AV_DARK 0x060A12
#define AV_BG 0x060A12 // page background, used for '.' pixels

static lv_obj_t *s_canvas;
// Full AV_PX_MAX buffer: 100x100 RGB565 = 20KB static, outside LVGL's pool.
static uint16_t s_buf[AV_N * AV_PX_MAX * AV_N * AV_PX_MAX];
static int s_px;
static int s_x;
static int s_y;
static bool s_created;
static int s_state = -1;
static int s_frame;
static int s_drawn_state = -1;
static int s_drawn_frame = -1;

static void av_apply(void)
{
    uint32_t acc = s_accent[s_state];
    for (int r = 0; r < AV_N; r++) {
        const char *row = s_frames[s_state][s_frame][r];
        for (int c = 0; c < AV_N; c++) {
            char ch = row[c];
            uint32_t col = (ch == '.') ? AV_BG
                         : (ch == 'b') ? AV_BODY
                         : (ch == 'd') ? AV_DARK
                                       : acc;
            lv_color_t color = lv_color_hex(col);
            for (int dy = 0; dy < s_px; dy++) {
                for (int dx = 0; dx < s_px; dx++) {
                    lv_canvas_set_px(
                        s_canvas, c * s_px + dx, r * s_px + dy, color,
                        LV_OPA_COVER);
                }
            }
        }
    }
    lv_obj_invalidate(s_canvas);
    s_drawn_state = s_state;
    s_drawn_frame = s_frame;
}

void trae_avatar_create(lv_obj_t *parent, int x, int y, int px)
{
    if (px < 1) {
        px = 1;
    }
    if (px > AV_PX_MAX) {
        px = AV_PX_MAX;
    }
    s_px = px;
    s_x = x;
    s_y = y;

    s_canvas = lv_canvas_create(parent);
    lv_obj_set_pos(s_canvas, x, y);
    lv_canvas_set_buffer(
        s_canvas, s_buf, AV_N * px, AV_N * px, LV_COLOR_FORMAT_RGB565);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    s_created = true;
    s_state = -1;
    s_frame = 0;
    s_drawn_state = -1;
    s_drawn_frame = -1;
}

void trae_avatar_set_state(int st)
{
    if (!s_created) {
        return;
    }
    if (st < PCMON_TRAE_IDLE || st > PCMON_TRAE_FAIL) {
        st = PCMON_TRAE_IDLE;
    }
    if (st == s_state) {
        return;
    }
    s_state = st;
    if (s_state != s_drawn_state || s_frame != s_drawn_frame) {
        av_apply();
    }
}

void trae_avatar_next_frame(void)
{
    if (!s_created || s_state < 0) {
        return;
    }
    s_frame ^= 1;
    av_apply();
}

void trae_avatar_layout(int x, int y, int px)
{
    if (!s_created) {
        return;
    }
    if (px < 1) {
        px = 1;
    }
    if (px > AV_PX_MAX) {
        px = AV_PX_MAX;
    }
    if (px == s_px && x == s_x && y == s_y) {
        return;
    }
    s_px = px;
    s_x = x;
    s_y = y;
    lv_canvas_set_buffer(
        s_canvas, s_buf, AV_N * px, AV_N * px, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, x, y);
    if (s_state >= 0) {
        av_apply();
    }
}
