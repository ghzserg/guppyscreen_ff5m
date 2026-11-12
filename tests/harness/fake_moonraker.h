#ifndef GUPPY_TEST_FAKE_MOONRAKER_H
#define GUPPY_TEST_FAKE_MOONRAKER_H

// A fake Moonraker: a real WebSocket JSON-RPC server on 127.0.0.1 that
// guppyscreen connects to like the real one.
//
// Requests are answered from a fixture file, a JSON object mapping
// method name -> result. Methods missing from the fixture get an empty
// object as result and are listed by unhandled(). Every request is
// recorded, and printer.gcode.script calls are also collected in gcodes().
//
// All methods are thread-safe: the server runs on its own libhv thread.

#include "hv/WebSocketServer.h"
#include "hv/hlog.h"
#include "hv/json.hpp"

#include <mutex>
#include <string>
#include <vector>

using json = nlohmann::json;

class FakeMoonraker {
 public:
  explicit FakeMoonraker(const std::string &fixture_path);
  ~FakeMoonraker();

  int port() const { return port_; }

  // Replace the result returned for `method`.
  void set_result(const std::string &method, const json &result);

  // Send notify_status_update to the connected client, e.g.
  // push_status({{"extruder", {{"temperature", 215.0}}}}).
  void push_status(const json &status);

  // Send any notification, e.g. push_notification("notify_klippy_ready", json::array()).
  void push_notification(const std::string &method, const json &params);

  bool client_connected() const;
  int request_count(const std::string &method) const;
  std::vector<json> requests(const std::string &method) const;  // their params
  std::vector<std::string> gcodes() const;                       // scripts, in order
  std::vector<std::string> unhandled() const;                    // methods not in the fixture

 private:
  void on_message(const WebSocketChannelPtr &channel, const std::string &msg);
  void send(const json &j);

  int port_;
  json results_;
  hv::WebSocketService service_;
  hv::WebSocketServer server_;

  mutable std::mutex mutex_;
  WebSocketChannelPtr client_;
  std::vector<std::pair<std::string, json>> requests_;
  std::vector<std::string> gcodes_;
  std::vector<std::string> unhandled_;
};

#endif  // GUPPY_TEST_FAKE_MOONRAKER_H
