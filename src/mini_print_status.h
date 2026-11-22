#ifndef __MINI_PRINT_STATUS__
#define __MINI_PRINT_STATUS__

#include "lvgl/lvgl.h"
#include <string>
#include "websocket_client.h"

class MiniPrintStatus {
 public:
  MiniPrintStatus(lv_obj_t *parent,
                  lv_event_cb_t cb,
                  void* user_data,
                    KWebSocketClient &ws);

  ~MiniPrintStatus();

  void show();
  void hide();
  lv_obj_t *get_container();

  void update_eta(const std::string &eta_str);
  void update_status(std::string &status_str);
  void update_progress(int p);
  void update_img(const std::string &img_path, size_t twidth);
  void reset();
#ifdef GUPPY_FF5M
  void update_clock(const char *time_text);
#endif

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *progress_bar;
  lv_obj_t *thumb;
  lv_obj_t *status_label;
#ifdef GUPPY_FF5M
  lv_obj_t *clock_label;
#endif
  std::string status;
  std::string eta;
};

#endif //__MINI_PRINT_STATUS__
