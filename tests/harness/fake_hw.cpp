#include "fake_hw.h"

#include "lv_drivers/display/fbdev.h"

namespace fake_hw {

std::atomic<bool> blanked{false};
std::atomic<int> brightness{-1};

static std::atomic<int> touch_x{0};
static std::atomic<int> touch_y{0};
static std::atomic<bool> touch_pressed{false};

void touch_set(int x, int y, bool pressed) {
  touch_x = x;
  touch_y = y;
  touch_pressed = pressed;
}

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data) {
  data->point.x = touch_x;
  data->point.y = touch_y;
  data->state = touch_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static lv_color_t framebuffer[WIDTH * HEIGHT];

lv_color_t pixel(int x, int y) {
  return framebuffer[y * WIDTH + x];
}

static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      framebuffer[y * WIDTH + x] = *colors++;
    }
  }
  lv_disp_flush_ready(drv);
}

// Mirrors hal_init() in src/main.cpp, minus the framebuffer, tslib and rotation.
void hal_init(lv_color_t primary, lv_color_t secondary) {
  static lv_color_t buf[WIDTH * 48];
  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, WIDTH * 48);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &draw_buf;
  disp_drv.flush_cb = flush;
  disp_drv.hor_res = WIDTH;
  disp_drv.ver_res = HEIGHT;
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

  lv_theme_t *th = lv_theme_default_init(NULL, primary, secondary, true, &notosemi_16);
  lv_disp_set_theme(disp, th);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);
}

}  // namespace fake_hw

// The fbdev_* functions guppyscreen calls directly.
extern "C" {
void fbdev_init(void) {}
void fbdev_exit(void) {}
void fbdev_blank(void) { fake_hw::blanked = true; }
void fbdev_unblank(void) { fake_hw::blanked = false; }
void fbdev_brightness(int value) { fake_hw::brightness = value; }
}
