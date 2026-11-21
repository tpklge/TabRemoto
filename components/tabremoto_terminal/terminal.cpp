#include "terminal.hpp"
#include "vterm.h"
#include <algorithm>
#include <cstdlib>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace tabremoto {
namespace {
void *allocate(size_t n, void *) {
#ifdef ESP_PLATFORM
  return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return calloc(1, n);
#endif
}
void release(void *p, void *) { free(p); }
VTermAllocatorFunctions allocator{allocate, release};
uint32_t rgb(VTermColor c, VTermScreen *screen) {
  vterm_screen_convert_color_to_rgb(screen, &c);
  return (uint32_t(c.rgb.red) << 16) | (uint32_t(c.rgb.green) << 8) |
         c.rgb.blue;
}
} // namespace
struct Terminal::Impl {
  VTerm *vt;
  VTermScreen *screen;
  std::vector<TerminalCell> history;
  unsigned start = 0, count = 0;
  bool cursor_visible = true;
  std::string response, pending;
  VTermScreenCallbacks callbacks{};
  Impl()
      : vt(vterm_new_with_allocator(rows, columns, &allocator, nullptr)),
        screen(vterm_obtain_screen(vt)), history(history_limit * columns) {
    vterm_set_utf8(vt, 1);
    callbacks.sb_pushline = [](int cols, const VTermScreenCell *cells,
                               void *user) {
      auto &self = *static_cast<Impl *>(user);
      unsigned slot = (self.start + self.count) % history_limit;
      if (self.count == history_limit) {
        slot = self.start;
        self.start = (self.start + 1) % history_limit;
      } else
        ++self.count;
      for (int c = 0; c < columns; c++)
        self.history[slot * columns + c] =
            c < cols ? self.convert(cells[c]) : TerminalCell{};
      return 1;
    };
    callbacks.sb_clear = [](void *user) {
      auto &s = *static_cast<Impl *>(user);
      s.start = s.count = 0;
      return 1;
    };
    callbacks.settermprop = [](VTermProp prop, VTermValue *value, void *user) {
      auto &s = *static_cast<Impl *>(user);
      if (prop == VTERM_PROP_CURSORVISIBLE)
        s.cursor_visible = value->boolean;
      return 1;
    };
    vterm_screen_set_callbacks(screen, &callbacks, this);
    vterm_screen_enable_altscreen(screen, 1);
    vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCREEN);
    vterm_output_set_callback(
        vt,
        [](const char *data, size_t n, void *user) {
          auto &s = *static_cast<Impl *>(user);
          if (s.response.size() + n <= 4096)
            s.response.append(data, n);
        },
        this);
    VTermColor fg{}, bg{};
    vterm_color_rgb(&fg, 238, 242, 248);
    vterm_color_rgb(&bg, 12, 15, 20);
    vterm_screen_set_default_colors(screen, &fg, &bg);
    vterm_screen_reset(screen, 1);
  }
  TerminalCell convert(const VTermScreenCell &cell) const {
    TerminalCell c;
    c.character = cell.attrs.conceal ? 0 : cell.chars[0];
    c.width = cell.width;
    c.underline = cell.attrs.underline;
    c.foreground = rgb(cell.fg, screen);
    c.background = rgb(cell.bg, screen);
    if (cell.attrs.reverse)
      std::swap(c.foreground, c.background);
    return c;
  }
  ~Impl() { vterm_free(vt); }
};
Terminal::Terminal() : impl(new Impl) {}
Terminal::~Terminal() = default;
Terminal::Terminal(Terminal &&) noexcept = default;
Terminal &Terminal::operator=(Terminal &&) noexcept = default;
void Terminal::feed(const char *p, size_t n) {
  std::string input;
  input.swap(impl->pending);
  input.append(p, n);
  size_t complete = input.size();
  if (complete) {
    size_t start = complete - 1;
    while (start > 0 && (uint8_t(input[start]) & 0xc0) == 0x80 &&
           complete - start < 4)
      --start;
    auto lead = uint8_t(input[start]);
    unsigned bytes = lead >= 0xc2 && lead <= 0xdf   ? 2
                     : lead >= 0xe0 && lead <= 0xef ? 3
                     : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                    : 1;
    if (bytes > complete - start) {
      impl->pending = input.substr(start);
      complete = start;
    }
  }
  vterm_input_write(impl->vt, input.data(), complete);
  vterm_screen_flush_damage(impl->screen);
}
void Terminal::key(TerminalKey key, unsigned modifiers) {
  vterm_keyboard_key(impl->vt, static_cast<VTermKey>(key), static_cast<VTermModifier>(modifiers & 7));
}
std::string Terminal::replies() {
  std::string out;
  out.swap(impl->response);
  return out;
}
std::string Terminal::text() const {
  std::vector<char> data(rows * columns * 6);
  auto n = vterm_screen_get_text(impl->screen, data.data(), data.size(),
                                 VTermRect{0, rows, 0, columns});
  return std::string(data.data(), n);
}
std::shared_ptr<const TerminalFrame> Terminal::frame() const {
  auto f = std::make_shared<TerminalFrame>();
  f->history = impl->count;
  f->cursor_visible = impl->cursor_visible;
  VTermPos pos{};
  vterm_state_get_cursorpos(vterm_obtain_state(impl->vt), &pos);
  f->cursor_row = pos.row;
  f->cursor_column = pos.col;
  f->cells.resize((f->history + rows) * columns);
  for (unsigned r = 0; r < impl->count; r++)
    std::copy_n(impl->history.begin() +
                    ((impl->start + r) % history_limit) * columns,
                columns, f->cells.begin() + r * columns);
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < columns; c++) {
      VTermScreenCell cell{};
      vterm_screen_get_cell(impl->screen, VTermPos{r, c}, &cell);
      f->cells[(f->history + r) * columns + c] = impl->convert(cell);
    }
  return f;
}
} // namespace tabremoto
