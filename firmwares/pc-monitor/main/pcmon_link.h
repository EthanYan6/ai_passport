#pragma once

#include <stdbool.h>

typedef void (*pcmon_link_cb_t)(const char *line, void *user);

bool pcmon_link_init(pcmon_link_cb_t cb, void *user);

void pcmon_link_send(const char *frame);
