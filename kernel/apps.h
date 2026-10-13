/* =========================================================================
 * AuroraOS userland apps: terminal, file manager, desktop shell
 * ========================================================================= */
#pragma once
#include <stdint.h>
#include "include/gui.h"

/* Terminal: scrollback console that runs a few built-in commands. */
void app_terminal_open(compositor_t *c, uint32_t x, uint32_t y);

/* File manager: browse the FAT32 volume starting at `path`. */
void app_filemanager_open(compositor_t *c, uint32_t x, uint32_t y,
                          const char *path);

/* Desktop shell: wallpaper extras, icons, taskbar; opens the default apps. */
void shell_init(compositor_t *c);
