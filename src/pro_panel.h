#ifndef __PRO_PANEL_H__
#define __PRO_PANEL_H__

#include "slider_container.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "grid_layout.h"

#include <mutex>
#include <vector>

class ProPanel : public NotifyConsumer {
 public:
  ProPanel(KWebSocketClient &c, std::mutex &l);
  ~ProPanel();

  void foreground();
#ifdef GUPPY_FF5M
  // Shows the circulation buttons if `circulation` (not on the fan screen)
  // and each calibration if Klipper has its macro; the calibrations take the
  // first row when the circulation is not there. Whether any button shows.
  bool configure(bool circulation, bool calibrate_extruders, bool calibrate_vfa);
#endif
  void handle_callback(lv_event_t *event);
  void consume(json &j);
  static void _handle_callback(lv_event_t *event) {
    ProPanel *panel = (ProPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  ButtonContainer air_circulation_internal_btn;
  ButtonContainer air_circulation_external_btn;
  ButtonContainer air_circulation_stop_btn;
  ButtonContainer calibrate_extruders_btn;
  ButtonContainer calibrate_vfa_btn;
#ifdef GUPPY_FF5M
  struct Placed {
    ButtonContainer &btn;
    bool configured_hidden;  // by the layout config: stays hidden
    GridLayout::Cell cell;   // as placed
  };
  std::vector<Placed> placed;  // internal, external, stop, extruders, vfa
#endif
};

#endif // __PRO_PANEL_H__
