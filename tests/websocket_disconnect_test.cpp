#include "doctest/doctest.h"
#include "app.h"
#include "websocket_client.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

TEST_CASE("disconnect releases unanswered requests and reconnect keeps new replies") {
  using namespace std::chrono_literals;
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  std::promise<void> opened, received;
  std::promise<bool> answered;
  server.set_handler("test.unanswered", [&](const json &) -> json {
    received.set_value();
    return json::value_t::discarded;
  });
  server.set_result("test.reconnected", "ok");

  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> pending = token;
  std::atomic<bool> old_callback_ran{false};
  std::atomic<bool> released_on_close{false};
  int connections = 0;
  KWebSocketClient client(nullptr);
  const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/websocket";
  client.connect(url.c_str(), [&] {
    if (++connections == 1) opened.set_value();
    else if (connections == 2) {
      client.send_jsonrpc("test.reconnected", [&](json &reply) {
        answered.set_value(reply.at("result") == "ok");
      });
    }
  }, [&] { released_on_close = pending.expired(); });
  REQUIRE(opened.get_future().wait_for(5s) == std::future_status::ready);
  client.send_jsonrpc("test.unanswered", [token, &old_callback_ran](json &) { old_callback_ran = true; });
  token.reset();
  REQUIRE(received.get_future().wait_for(5s) == std::future_status::ready);
  CHECK_FALSE(pending.expired());

  server.drop_client();
  auto reply = answered.get_future();
  REQUIRE(reply.wait_for(5s) == std::future_status::ready);
  CHECK(reply.get());
  CHECK(released_on_close.load());
  CHECK(pending.expired());
  CHECK_FALSE(old_callback_ran);
  client.stop();
}
