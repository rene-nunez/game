#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

#include <Network.h>

// handlers can dispatch from data[0] the `type` without parsing the rest
enum class msg_type : uint8_t {
  heartbeat = 0x01,
  game_state = 0x02,
  player_input = 0x10,
};

// Payload of a heartbeat is 8 bytes total
struct __attribute__((packed)) heartbeat_msg {
  msg_type type = msg_type::heartbeat;
  uint32_t tick = 0;
  uint8_t role = ROLE_HOST;
  uint16_t reserved = 0;
};

class message_handler {
  public:
    bool begin(uint8_t role);
    bool send(const void* payload, size_t len);
    void on_message(msg_type type, std::function<void(const uint8_t*, size_t)> callback);
    uint8_t role() const;

  private:
    static constexpr size_t MAX_HANDLERS = 8;

    struct handler_entry {
      msg_type type = msg_type::heartbeat;
      std::function<void(const uint8_t*, size_t)> callback;
    };

    handler_entry _handlers[MAX_HANDLERS] = {};
    size_t _handler_count = 0;

    void _dispatch(const uint8_t* data, int len);
};
