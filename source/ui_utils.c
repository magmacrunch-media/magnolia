#include <grrlib.h>
#include <stdio.h>
#include <string.h>
#include "ui_utils.h"
#include "text.h"
#include "ui_geom.h"
#include "renderer.h"


static int overscan_pct = 6;

/* The drop shadow under every string this module draws, and the border colour.
   Both were compile-time constants until a game put dark text on a pale panel:
   a black shadow under a red card rank turns the glyph into a smudge, and a
   cyan border on a lava table reads as another program's chrome. Settable for
   the same reason the overscan is -- it is a property of the game's look, not
   of the drawing. Defaults are what they always were, so a caller that never
   touches them sees no change. */
static u32 shadow_color = 0x000000FF;
static u32 border_color = 0x00D4FFFF;

/* The running video mode, as a value, for the arithmetic in ui_geom.
   Built per call rather than cached: the geometry has always been read live from
   the video mode, and a cache would be a second thing to invalidate for no gain
   at three integer copies. */
static UiGeom geom(void) {
    return ui_geom_make(renderer_screen_width(), renderer_screen_height(),
                        overscan_pct);
}

void ui_set_overscan_pct(int pct) {
    if (pct < 0)  pct = 0;
    if (pct > 20) pct = 20;
    overscan_pct = pct;
}

int ui_get_overscan_pct(void) { return overscan_pct; }

void ui_set_shadow_color(u32 color) { shadow_color = color; }
u32  ui_get_shadow_color(void)      { return shadow_color; }

void ui_set_border_color(u32 color) { border_color = color; }
u32  ui_get_border_color(void)      { return border_color; }

int ui_safe_x(void) { UiGeom g = geom(); return ui_geom_safe_x(&g); }
int ui_safe_y(void) { UiGeom g = geom(); return ui_geom_safe_y(&g); }
int ui_safe_w(void) { UiGeom g = geom(); return ui_geom_safe_w(&g); }
int ui_safe_h(void) { UiGeom g = geom(); return ui_geom_safe_h(&g); }

int ui_map_x(int design_x) { UiGeom g = geom(); return ui_geom_map_x(&g, design_x); }
int ui_map_y(int design_y) { UiGeom g = geom(); return ui_geom_map_y(&g, design_y); }
int ui_map_w(int design_w) { UiGeom g = geom(); return ui_geom_map_w(&g, design_w); }
int ui_map_h(int design_h) { UiGeom g = geom(); return ui_geom_map_h(&g, design_h); }

unsigned int ui_map_size(unsigned int design_size) {
    UiGeom g = geom();
    return ui_geom_map_size(&g, design_size);
}

void ui_draw_text_shadow(int design_x, int design_y, const char *text,
                         unsigned int design_size, u32 color) {
    if (!ttf_font) return;
    int x = ui_map_x(design_x);
    int y = ui_map_y(design_y);
    unsigned int size = ui_map_size(design_size);
    text_draw(x + 2, y + 2, text, size, shadow_color);
    text_draw(x, y, text, size, color);
}

void ui_draw_centered_text(int design_y, const char *text,
                           unsigned int design_size, u32 color) {
    if (!ttf_font) return;
    unsigned int size = ui_map_size(design_size);
    u32 w = text_width(text, size);
    int x = ui_safe_x() + (ui_safe_w() - (int)w) / 2;
    int y = ui_map_y(design_y);
    if (x < 0) x = 0;
    text_draw(x + 2, y + 2, text, size, shadow_color);
    text_draw(x, y, text, size, color);
}

void ui_draw_border(void) {
    int x = ui_safe_x(), y = ui_safe_y();
    int w = ui_safe_w(), h = ui_safe_h();
    int t = 3;
    GRRLIB_Rectangle(x, y, w, t, border_color, true);
    GRRLIB_Rectangle(x, y + h - t, w, t, border_color, true);
    GRRLIB_Rectangle(x, y, t, h, border_color, true);
    GRRLIB_Rectangle(x + w - t, y, t, h, border_color, true);
}

int ui_text_width(const char *text, unsigned int design_size) {
    if (!ttf_font || !text) return 0;
    UiGeom g = geom();
    u32 w = text_width(text, ui_geom_map_size(&g, design_size));
    /* Back out of screen space into design space, so the result composes with
       the design-space coordinates the caller is working in. */
    return ui_geom_to_design_w(&g, w);
}

void ui_draw_text_centered_in(int design_x, int design_y,
                              int design_w, int design_h,
                              const char *text, unsigned int design_size,
                              u32 color) {
    if (!ttf_font || !text) return;
    unsigned int size = ui_map_size(design_size);
    u32 tw = text_width(text, size);

    int x = ui_map_x(design_x) + (ui_map_w(design_w) - (int)tw) / 2;
    /* GRRLIB places text by its top-left, and a glyph box is close enough to the
       point size that centring on it reads correctly at these sizes. */
    int y = ui_map_y(design_y) + (ui_map_h(design_h) - (int)size) / 2;

    text_draw(x + 2, y + 2, text, size, shadow_color);
    text_draw(x, y, text, size, color);
}

/* GX has no rounded-rectangle primitive, so a radius is built from a middle
   band, two inset side bands and four quarter-circles. Cheap at UI scale, and it
   keeps every panel in the game the same shape. */
static void panel_fill(int x, int y, int w, int h, u32 color, int r) {
    if ((color & 0xFF) == 0) return;

    if (r <= 0) {
        GRRLIB_Rectangle((f32)x, (f32)y, (f32)w, (f32)h, color, true);
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    GRRLIB_Rectangle((f32)x, (f32)(y + r), (f32)w, (f32)(h - 2 * r), color, true);
    GRRLIB_Rectangle((f32)(x + r), (f32)y, (f32)(w - 2 * r), (f32)r, color, true);
    GRRLIB_Rectangle((f32)(x + r), (f32)(y + h - r), (f32)(w - 2 * r), (f32)r, color, true);

    GRRLIB_Circle((f32)(x + r),         (f32)(y + r),         (f32)r, color, true);
    GRRLIB_Circle((f32)(x + w - r),     (f32)(y + r),         (f32)r, color, true);
    GRRLIB_Circle((f32)(x + r),         (f32)(y + h - r),     (f32)r, color, true);
    GRRLIB_Circle((f32)(x + w - r),     (f32)(y + h - r),     (f32)r, color, true);
}

void ui_draw_panel(int design_x, int design_y, int design_w, int design_h,
                   u32 fill, u32 outline, int radius) {
    int x = ui_map_x(design_x);
    int y = ui_map_y(design_y);
    int w = ui_map_w(design_w);
    int h = ui_map_h(design_h);
    int r = ui_map_w(radius);

    panel_fill(x, y, w, h, fill, r);

    if ((outline & 0xFF) != 0) {
        /* Drawn as a rectangle rather than following the corner arcs: at these
           radii the difference is invisible on a CRT and the arc version costs
           four more circles a panel. */
        GRRLIB_Rectangle((f32)x, (f32)y, (f32)w, (f32)h, outline, false);
    }
}

void ui_draw_dim_overlay(u32 color) {
    GRRLIB_Rectangle(0.0f, 0.0f,
                     (f32)renderer_screen_width(), (f32)renderer_screen_height(),
                     color, true);
}

/* The measurer ui_geom is handed. It cannot do this itself -- the font lives on
   this side of the seam -- so the wrap asks, exactly as input_state is told which
   buttons are down rather than reading the hardware. */
static int measure_design_w(const char *text, unsigned int design_size, void *ctx) {
    (void)ctx;
    return ui_text_width(text, design_size);
}

int ui_draw_text_wrapped(int design_x, int design_y, int design_w,
                         const char *text, unsigned int design_size,
                         u32 color, int line_spacing) {
    if (!ttf_font || !text) return design_y;
    if (line_spacing <= 0) line_spacing = (int)design_size + 4;

    char line[UI_WRAP_MAX];
    int y = design_y;
    const char *p = text;

    /* One line drawn per line settled. Where the breaks fall is ui_geom's
       decision and is asserted there; this loop only puts them on the screen. */
    while ((p = ui_geom_wrap_next(p, line, (int)sizeof(line), design_w,
                                  design_size, measure_design_w, NULL)) != NULL) {
        ui_draw_text_shadow(design_x, y, line, design_size, color);
        y += line_spacing;
    }
    return y;
}
