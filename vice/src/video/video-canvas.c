/*
 * video-canvas.c
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

/* #define DEBUG_VIDEO */

#ifdef DEBUG_VIDEO
#define DBG(_x_) log_printf  _x_
#else
#define DBG(_x_)
#endif

#include "vice.h"

#include "videoarch.h"

#include <stdio.h>
#include <stdlib.h>

#include "lib.h"
#include "log.h"
#include "machine.h"
#include "types.h"
#include "video-canvas.h"
#include "video-color.h"
#include "video-render.h"
#include "video.h"
#include "viewport.h"

#ifdef __LIBRETRO__
extern int retroXS, retroYS, retrow, retroh;
#endif

#define TRACKED_CANVAS_MAX 2

/** \brief Used to enable video_canvas_refresh_all_tracked() */
static video_canvas_t *tracked_canvas[TRACKED_CANVAS_MAX];

/* Temporary! */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* called from raster/raster-resources.c:raster_resources_chip_init */
video_canvas_t *video_canvas_init(void)
{
    int i;
    video_canvas_t *canvas;

    canvas = lib_calloc(1, sizeof(video_canvas_t));
    DBG(("video_canvas_init %p", canvas));

    canvas->videoconfig = lib_calloc(1, sizeof(video_render_config_t));

    canvas->draw_buffer = lib_calloc(1, sizeof(draw_buffer_t));
    canvas->viewport = lib_calloc(1, sizeof(viewport_t));
    canvas->geometry = lib_calloc(1, sizeof(geometry_t));

    video_arch_canvas_init(canvas);

    for (i = 0; i < TRACKED_CANVAS_MAX; i++) {
        if (!tracked_canvas[i]) {
            tracked_canvas[i] = canvas;
            break;
        }
    }

    if (i == TRACKED_CANVAS_MAX) {
        log_error(LOG_DEFAULT, "Creating more than expected video_canvas_t, monitor will not refresh this canvas after each command");
    }

    return canvas;
}

void video_canvas_shutdown(video_canvas_t *canvas)
{
    int i;

    if (canvas != NULL) {
        /* Remove canvas from tracking */
        for (i = 0; i < TRACKED_CANVAS_MAX; i++) {
            if (tracked_canvas[i] == canvas) {
                tracked_canvas[i] = NULL;
                break;
            }
        }

        lib_free(canvas->videoconfig);
        lib_free(canvas->draw_buffer);
        lib_free(canvas->viewport);
        lib_free(canvas->geometry);
        lib_free(canvas);
    }
}

void video_canvas_render(video_canvas_t *canvas, uint8_t *trg, int width,
                         int height, int xs, int ys, int xt, int yt,
                         int pitcht)
{
    viewport_t *viewport = canvas->viewport;
#ifdef VIDEO_SCALE_SOURCE
    xs /= canvas->videoconfig->scalex;
    ys /= canvas->videoconfig->scaley;
#endif

    /* when the color encoding changed, the palette must be recalculated */
    if (viewport->crt_type != canvas->crt_type) {
        canvas->videoconfig->color_tables.updated = 0;
        canvas->crt_type = viewport->crt_type;
    }

    if (!canvas->videoconfig->color_tables.updated) { /* update colors as necessary */
        video_color_update_palette(canvas);
    }
    video_render_main(canvas->videoconfig, canvas->draw_buffer->draw_buffer,
                      trg, width, height, xs, ys, xt, yt,
                      canvas->draw_buffer->draw_buffer_width, pitcht,
                      viewport);
}

/** \brief Force refresh all tracked canvases.
 *
 * Added to enable visible updates each time the monitor
 * prompts for input.
 */
void video_canvas_refresh_all_tracked(void)
{
    int i;

    for (i = 0; i < TRACKED_CANVAS_MAX; i++) {
        if (tracked_canvas[i]) {
            video_canvas_refresh_all(tracked_canvas[i]);
        }
    }
}

void video_canvas_refresh_all(video_canvas_t *canvas)
{
    viewport_t *viewport;
    geometry_t *geometry;

    if (video_disabled_mode) {
        return;
    }

    viewport = canvas->viewport;
    geometry = canvas->geometry;

#ifdef __LIBRETRO__
#ifdef RETRO_DEBUG
    printf("canvas:XS:%d YS:%d XI:%d YI:%d W:%d H:%d\n",
                         viewport->first_x
                         + geometry->extra_offscreen_border_left,
                         viewport->first_line,
                         viewport->x_offset,
                         viewport->y_offset,
                         MIN(canvas->draw_buffer->canvas_width,
                             geometry->screen_size.width - viewport->first_x),
                         MIN(canvas->draw_buffer->canvas_height,
                             viewport->last_line - viewport->first_line + 1));
#endif
	retroXS = viewport->first_x + geometry->extra_offscreen_border_left;
	retroYS = viewport->first_line;
	retrow = MIN(canvas->draw_buffer->canvas_width, geometry->screen_size.width - viewport->first_x);
	retroh = MIN(canvas->draw_buffer->canvas_height, viewport->last_line - viewport->first_line + 1);
#endif /* __LIBRETRO__ */
    video_canvas_refresh(canvas,
                         viewport->first_x
                         + geometry->extra_offscreen_border_left,
                         viewport->first_line,
                         viewport->x_offset,
                         viewport->y_offset,
                         MIN(canvas->draw_buffer->canvas_width,
                             geometry->screen_size.width - viewport->first_x),
                         MIN(canvas->draw_buffer->canvas_height,
                             viewport->last_line - viewport->first_line + 1));
}

int video_canvas_palette_set(struct video_canvas_s *canvas,
                             struct palette_s *palette)
{
    struct palette_s *old_palette;

    if (palette == NULL) {
        return 0;
    }

    old_palette = canvas->palette;

    if (canvas->created) {
        if (video_canvas_set_palette(canvas, palette) < 0) {
            return -1;
        }
    } else {
        canvas->palette = palette;
    }

    if (old_palette != NULL) {
        video_color_palette_free(old_palette);
    }

#if 0 /* WTF this was causing each frame to be rendered twice */
   if (canvas->created) {
       video_canvas_refresh_all(canvas);
   }
#endif

    return 0;
}

void video_canvas_create_set(struct video_canvas_s *canvas)
{
    canvas->created = 1;
}
