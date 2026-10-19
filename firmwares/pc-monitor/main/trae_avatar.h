#pragma once

#include "lvgl.h"

// 10x10 pixel companion for the TRAE battle page. Pixels are LVGL objects
// created once by trae_avatar_create(); state switches and frame animation
// only restyle them, so it is cheap to call from the render loop.
void trae_avatar_create(lv_obj_t *parent, int x, int y, int px);

// st uses the pcmon_trae_state_t values (0 = idle ... 5 = fail).
void trae_avatar_set_state(int st);

// Advance the current state's animation; call from the render tick.
void trae_avatar_next_frame(void);

// Move/resize the sprite at runtime (approval mode shrinks it to make room
// for the command box). No-op when the layout is unchanged.
void trae_avatar_layout(int x, int y, int px);
