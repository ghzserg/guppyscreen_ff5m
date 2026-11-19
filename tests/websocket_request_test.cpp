#include "doctest/doctest.h"
#include "app.h"
#include "websocket_client.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

TEST_CASE("parallel JSON-RPC requests keep their own replies and can send from a callback") {
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  server.set_handler("test.echo", [](const json &params) { return params.at("token"); });
  server.set_result("test.nested", "ok");

  std::promise<void> connected, nested;
  std::atomic<int> received{0}, mismatched{0};
  KWebSocketClient client(nullptr);
  const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/websocket";
  client.connect(url.c_str(), [&] { connected.set_value(); }, [] {});
  REQUIRE(connected.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);

  constexpr int count = 128;
  std::atomic<int> arrivals{0};
  auto sender = [&](int first) {
    for (int token = first; token < 2 * count; token += 2) {
      ++arrivals;
      while (arrivals < 2 * (token / 2 + 1)) std::this_thread::yield();
      client.send_jsonrpc("test.echo", {{"token", token}}, [&, token](json &reply) {
        if (reply.at("result") != token) ++mismatched;
        if (++received == 2 * count) {
          client.send_jsonrpc("test.nested", [&](json &next) {
            if (next.at("result") != "ok") ++mismatched;
            nested.set_value();
          });
        }
      });
    }
  };
  std::thread second(sender, 1);
  sender(0);
  second.join();
  CHECK(nested.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  client.stop();
  CHECK(received == 2 * count);
  CHECK(mismatched == 0);
}
