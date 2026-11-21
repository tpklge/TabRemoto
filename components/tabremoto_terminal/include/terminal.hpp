#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace tabremoto {
enum class TerminalKey { None,Enter,Tab,Backspace,Escape,Up,Down,Left,Right,Insert,Delete,Home,End,PageUp,PageDown,F1=257,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12 };
struct TerminalCell {
  uint32_t character = 0, foreground = 0xeef2f8, background = 0x0c0f14;
  uint8_t width = 1;
  bool underline = false;
};
struct TerminalFrame {
  int columns = 100, rows = 29, history = 0, cursor_row = 0, cursor_column = 0;
  bool cursor_visible = true;
  std::vector<TerminalCell> cells;
};
// Owner is the SSH worker only. Published frames are immutable across tasks.
class Terminal {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  static constexpr int columns = 100, rows = 29, history_limit = 128;
  Terminal();
  ~Terminal();
  Terminal(Terminal &&) noexcept;
  Terminal &operator=(Terminal &&) noexcept;
  void feed(const char *, size_t);
  std::string text() const;
  std::string replies();
  void key(TerminalKey, unsigned modifiers=0);
  std::shared_ptr<const TerminalFrame> frame() const;
};
} // namespace tabremoto
