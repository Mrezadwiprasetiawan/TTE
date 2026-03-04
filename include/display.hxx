#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <input_handler.hxx> // shared Event, Dir
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

enum class CursorBlink {
  def = 0,
  block = 1,
  steadyBlock = 2,
  underline = 3,
  steadyUnderline = 4,
  bar = 5,
  steadyBar = 6,
};

struct ColorSpan {
  int row, start, end;
  std::array<int, 3> RGB;
};

//  Macros (undef'd at bottom of file)
// Append ANSI 24-bit colour escape into renderBuf_
#define BUF_COLOR(layer, r, g, b)                                              \
  do {                                                                         \
    char _t[32];                                                               \
    renderBuf_.append(                                                         \
        _t, snprintf(_t, 32, "\x1b[" layer ";2;%d;%d;%dm", (r), (g), (b)));    \
  } while (0)

#define BUF_BG(r, g, b) BUF_COLOR("48", r, g, b)
#define BUF_FG(r, g, b) BUF_COLOR("38", r, g, b)

// Write ANSI 24-bit colour escape directly to stdout
#define WRITE_COLOR(layer, r, g, b)                                            \
  do {                                                                         \
    char _t[32];                                                               \
    write_raw(_t,                                                              \
              snprintf(_t, 32, "\x1b[" layer ";2;%d;%d;%dm", (r), (g), (b)));  \
  } while (0)

// Generate scroll member functions
#define SCROLL_DEC(name, field)                                                \
  bool scroll_##name(int dist) {                                               \
    if (field >= dist) {                                                       \
      field -= dist;                                                           \
      mark_changed();                                                          \
      return true;                                                             \
    }                                                                          \
    return false;                                                              \
  }

#define SCROLL_INC(name, field, limit)                                         \
  bool scroll_##name(int dist) {                                               \
    if (field + dist + (limit) <= (int)data_.size() + 1) {                     \
      field += dist;                                                           \
      mark_changed();                                                          \
      return true;                                                             \
    }                                                                          \
    return false;                                                              \
  }

class Display {

  void write_raw(const char *s, size_t n) {
#ifndef _WIN32
    ::write(STDOUT_FILENO, s, n);
#else
    DWORD w;
    WriteConsoleA(hOut_, s, (DWORD)n, &w, nullptr);
#endif
  }
  void write_raw(const std::string &s) { write_raw(s.data(), s.size()); }

  // ── Render-buffer colour helpers ─────────────────────────────────────────

  void buf_bg(const std::array<int, 3> &c) { BUF_BG(c[0], c[1], c[2]); }
  void buf_fg(const std::array<int, 3> &c) { BUF_FG(c[0], c[1], c[2]); }

  static bool rgb_eq(const std::array<int, 3> &a, const std::array<int, 3> &b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
  }

  // ── Viewport / cursor geometry ───────────────────────────────────────────

  int content_width() const {
    return lineNumbering_ ? width_ - lnWidth_ : width_;
  }

  // Length of the data row currently under the cursor
  int cur_row_len() const {
    int dr = startRowData_ + (cursorPos_[0] - 1);
    return (dr < (int)data_.size()) ? (int)data_[dr].size() : 0;
  }

  void clamp_col_to_row() {
    int len = cur_row_len();
    int maxCol = (len > startColData_)
                     ? std::min(content_width() + 1, len - startColData_ + 1)
                     : 1;
    cursorPos_[1] = std::max(1, std::min(cursorPos_[1], maxCol));
  }

  // ── Physical terminal cursor ─────────────────────────────────────────────
  //
  // emit_cursor_ansi: sends ANSI escape to move the terminal cursor.
  // (r, c) are screen-data-relative (1-based); column is automatically
  // shifted right by lnWidth_ so the cursor never overlaps the gutter.
  // Does NOT touch cursorPos_.

  void emit_cursor_ansi(int r, int c) {
    char buf[32];
    int termCol = c + (lineNumbering_ ? lnWidth_ : 0);
    write_raw(buf, snprintf(buf, 32, "\x1b[%d;%dH", r, termCol));
  }

  void buf_line_number(int dataRow) {
    BUF_BG(lnBg_[0], lnBg_[1], lnBg_[2]);
    BUF_FG(lnFg_[0], lnFg_[1], lnFg_[2]);
    char tmp[16];
    renderBuf_.append(
        tmp, snprintf(tmp, sizeof(tmp), "%*d ", lnWidth_ - 1, dataRow + 1));
  }

  Display() {
#ifdef _WIN32
    hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut_ != INVALID_HANDLE_VALUE) {
      GetConsoleMode(hOut_, &outModeOrig_);
      SetConsoleMode(hOut_, outModeOrig_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif

#ifndef _WIN32
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
      width_ = w.ws_col;
      height_ = w.ws_row;
    } else {
      width_ = 80;
      height_ = 24;
    }
#else
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
      width_ = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      height_ = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
      width_ = 80;
      height_ = 24;
    }
#endif

    renderBuf_.reserve((size_t)width_ * height_ * 50);
    height_ -= extraHeight_;
    enterAlternateScreen();
  }
  bool alt_screen_ = false;

#ifdef _WIN32
  HANDLE hOut_ = nullptr;
  DWORD outModeOrig_ = 0;
#endif

  int width_, height_;
  int startRowData_ = 0, startColData_ = 0;

  // Line-number gutter
  bool lineNumbering_ = true;
  int lnWidth_ = 5;                           // digits + 1 trailing space
  std::array<int, 3> lnBg_ = {30, 30, 30};    // dark grey background
  std::array<int, 3> lnFg_ = {120, 120, 120}; // mid grey foreground

  const int extraHeight_ = 3;

  // cursorPos_: screen-data-relative, 1-based.
  // Row 1 = top visible line, Col 1 = leftmost visible data column.
  // Terminal column = cursorPos_[1] + lnWidth_ (handled in emit_cursor_ansi).
  std::array<int, 2> cursorPos_ = {1, 1};
  bool isChanged_ = false;

  std::vector<std::vector<char>> data_;
  std::array<int, 3> mainBg_ = {0, 0, 0};
  std::array<int, 3> mainFg_ = {255, 255, 255};
  std::vector<ColorSpan> bg_, fg_;

  std::string renderBuf_;

public:
  Display(const Display &) = delete;
  Display &operator=(const Display &) = delete;

  static Display &getInstance() {
    static Display instance;
    return instance;
  }

  ~Display() {
    exitAlternateScreen();
#ifdef _WIN32
    if (hOut_)
      SetConsoleMode(hOut_, outModeOrig_);
#endif
    showCursor();
  }

  void enterAlternateScreen() {
    if (alt_screen_)
      return;
    write_raw("\x1b[?1049h", 8);
    alt_screen_ = true;
  }
  void exitAlternateScreen() {
    if (!alt_screen_)
      return;
    write_raw("\x1b[?1049l", 8);
    alt_screen_ = false;
  }

  void clear() { write_raw("\x1b[2J\x1b[H", 7); }
  void hideCursor() { write_raw("\x1b[?25l", 6); }
  void showCursor() { write_raw("\x1b[?25h", 6); }

  void setCursorBlink(CursorBlink b) {
    char buf[16];
    write_raw(buf, snprintf(buf, 16, "\x1b[%d q", (int)b));
  }

  // move_cursor: update internal screen-data position AND move terminal cursor.
  // (r, c) are screen-data-relative (1-based).
  void move_cursor(int r, int c) {
    cursorPos_ = {r, c};
    emit_cursor_ansi(r, c);
  }
  void move_cursor(std::array<int, 2> pos) { move_cursor(pos[0], pos[1]); }

  // move_cursor_relative: move the cursor by `dist` in direction `dir`,
  // scrolling the viewport when the cursor would leave the visible area.
  // cursorPos_ stays screen-data-relative throughout; the terminal cursor is
  // repositioned at the end via emit_cursor_ansi (no gutter offset).
  void move_cursor_relative(Dir dir, int dist) {
    if (dist <= 0)
      return;
    switch (dir) {
    case Dir::UP: {
      if (cursorPos_[0] - dist >= 1) {
        // Cursor stays on screen
        cursorPos_[0] -= dist;
      } else {
        // Hit the top of the screen — scroll the viewport
        int rem = dist - (cursorPos_[0] - 1);
        cursorPos_[0] = 1;
        scroll_up(rem);
      }
      clamp_col_to_row();
      break;
    }
    case Dir::BOT: {
      int target = startRowData_ + (cursorPos_[0] - 1) + dist;
      if (target >= (int)data_.size())
        break; // Don't go past the last data row
      if (cursorPos_[0] + dist <= height_) {
        // Cursor stays on screen
        cursorPos_[0] += dist;
      } else {
        // Hit the bottom of the screen — scroll the viewport
        int over = (cursorPos_[0] + dist) - height_;
        cursorPos_[0] = height_;
        scroll_bot(over);
      }
      clamp_col_to_row();
      break;
    }
    case Dir::RGT: {
      int cw = content_width();
      int maxCol = std::min(cw, cur_row_len() - startColData_);
      if (cursorPos_[1] + dist <= maxCol + 1) {
        // Cursor stays on screen
        cursorPos_[1] += dist;
      } else if (cursorPos_[1] <= maxCol) {
        // Clamp to end of visible row content
        cursorPos_[1] = maxCol;
      } else if (scroll_rgt(dist)) {
        // Scrolled right — keep cursor within new visible content
        cursorPos_[1] = std::min(cursorPos_[1],
                                 std::min(cw, cur_row_len() - startColData_));
      }
      break;
    }
    case Dir::LFT: {
      if (cursorPos_[1] - dist >= 1) {
        // Cursor stays on screen
        cursorPos_[1] -= dist;
      } else {
        // Hit the left edge — scroll the viewport
        int rem = dist - (cursorPos_[1] - 1);
        cursorPos_[1] = 1;
        scroll_lft(rem);
      }
      break;
    }
    }
    // cursorPos_ is screen-data-relative; emit_cursor_ansi adds lnWidth_
    // offset.
    emit_cursor_ansi(cursorPos_[0], cursorPos_[1]);
    mark_changed();
  }

  // Returns {dataRow, dataCol} (0-based) of the cursor in the full data buffer.
  std::array<int, 2> get_cursor_data_pos() {
    return {cursorPos_[0] + startRowData_ - 1,
            cursorPos_[1] + startColData_ - 1};
  }

  void scroll_to(int y, int x) {
    startRowData_ = y;
    startColData_ = x;
    mark_changed();
  }

  // Move cursor to the first column of the current line.
  void go_line_start() {
    startColData_ = 0;
    cursorPos_[1] = 1;
    mark_changed();
  }

  // Move cursor to the last character of the current line,
  // scrolling horizontally so it is visible.
  void go_line_end() {
    int dataRow = get_cursor_data_pos()[0];
    if (dataRow >= (int)data_.size())
      return;
    int len = (int)data_[dataRow].size();
    int cw = content_width();
    if (len == 0) {
      startColData_ = 0;
      cursorPos_[1] = 1;
    } else if (len <= cw) {
      startColData_ = 0;
      cursorPos_[1] = len + 1;
    } else {
      startColData_ = len - cw + 1;
      cursorPos_[1] = cw + 1;
    }
    mark_changed();
  }

  SCROLL_DEC(up, startRowData_)
  SCROLL_DEC(lft, startColData_)
  SCROLL_INC(bot, startRowData_, height_)

  bool scroll_rgt(int dist) {
    if (startRowData_ + (cursorPos_[0] - 1) >= (int)data_.size())
      return false;
    int row = startRowData_ + (cursorPos_[0] - 1);
    int cw = content_width();
    if (startColData_ + dist + cw <= (int)data_[row].size() + 1) {
      startColData_ += dist;
      mark_changed();
      return true;
    }
    return false;
  }

  void fgRGB(int r, int g, int b) { WRITE_COLOR("38", r, g, b); }
  void bgRGB(int r, int g, int b) { WRITE_COLOR("48", r, g, b); }
  void reset() { write_raw("\x1b[0m", 4); }

  void notify_resize(int termW, int termH) {
    width_ = termW;
    height_ = termH - extraHeight_;
    // Keep cursor within new screen bounds
    cursorPos_[0] = std::min(cursorPos_[0], height_);
    cursorPos_[1] = std::min(cursorPos_[1], content_width());
    mark_changed();
  }

  int get_width() const { return content_width(); }
  int get_height() const { return height_; }
  std::array<int, 2> get_cursor_pos() const { return cursorPos_; }

  void set_line_numbering(bool val) {
    lineNumbering_ = val;
    mark_changed();
  }
  bool get_line_numbering_state() const { return lineNumbering_; }

  void set_ln_width(int w) {
    lnWidth_ = w;
    mark_changed();
  }
  int get_ln_width() const { return lnWidth_; }

  void set_ln_colors(std::array<int, 3> bg, std::array<int, 3> fg) {
    lnBg_ = bg;
    lnFg_ = fg;
    mark_changed();
  }

  void set_row_data(int row, const std::vector<char> &buf) {
    if (row < 0)
      return;
    if (row >= (int)data_.size())
      data_.resize(row + 1);
    data_[row] = buf;
    mark_changed();
  }
  void add_data(const std::vector<std::vector<char>> &buf) {
    for (const auto &r : buf)
      data_.push_back(r);
    mark_changed();
  }
  void set_data(const std::vector<std::vector<char>> &d) {
    data_ = d;
    mark_changed();
  }

  std::vector<std::vector<char>> &get_data() { return data_; }
  const std::vector<std::vector<char>> &get_data() const { return data_; }

  void set_bg_span(ColorSpan s) {
    bg_.push_back(s);
    mark_changed();
  }
  void set_fg_span(ColorSpan s) {
    fg_.push_back(s);
    mark_changed();
  }
  void clear_color_spans() {
    bg_.clear();
    fg_.clear();
    mark_changed();
  }

  void set_main_bg(int r, int g, int b) {
    mainBg_ = {r, g, b};
    mark_changed();
  }
  void set_main_fg(int r, int g, int b) {
    mainFg_ = {r, g, b};
    mark_changed();
  }

  void mark_changed() { isChanged_ = true; }

  void render() {
    if (!isChanged_)
      return;

    renderBuf_.clear();
    renderBuf_.append("\x1b[2J\x1b[H", 7);

    std::array<int, 3> curBg = {-1, -1, -1};
    std::array<int, 3> curFg = {-1, -1, -1};

    buf_bg(mainBg_);
    curBg = mainBg_;
    buf_fg(mainFg_);
    curFg = mainFg_;

    for (int y = 0; y < height_; ++y) {
      int row = startRowData_ + y;

      if (lineNumbering_) {
        buf_line_number(row);
        if (!rgb_eq(mainBg_, curBg)) {
          buf_bg(mainBg_);
          curBg = mainBg_;
        }
        if (!rgb_eq(mainFg_, curFg)) {
          buf_fg(mainFg_);
          curFg = mainFg_;
        }
      }

      ColorSpan bgSpan{-1, 0, 0, mainBg_};
      ColorSpan fgSpan{-1, 0, 0, mainFg_};
      for (auto &b : bg_)
        if (b.row == row) {
          bgSpan = b;
          break;
        }
      for (auto &f : fg_)
        if (f.row == row) {
          fgSpan = f;
          break;
        }

      const int cw = content_width();
      int x = 0;
      while (x < cw) {
        bool inBg = (bgSpan.row == row && x >= bgSpan.start && x <= bgSpan.end);
        bool inFg = (fgSpan.row == row && x >= fgSpan.start && x <= fgSpan.end);

        std::array<int, 3> wantBg = inBg ? bgSpan.RGB : mainBg_;
        std::array<int, 3> wantFg = inFg ? fgSpan.RGB : mainFg_;

        if (!rgb_eq(wantBg, curBg)) {
          buf_bg(wantBg);
          curBg = wantBg;
        }
        if (!rgb_eq(wantFg, curFg)) {
          buf_fg(wantFg);
          curFg = wantFg;
        }

        while (x < cw) {
          int cx = startColData_ + x;
          bool ib = (bgSpan.row == row && x >= bgSpan.start && x <= bgSpan.end);
          bool iff =
              (fgSpan.row == row && x >= fgSpan.start && x <= fgSpan.end);
          if (!rgb_eq(ib ? bgSpan.RGB : mainBg_, wantBg))
            break;
          if (!rgb_eq(iff ? fgSpan.RGB : mainFg_, wantFg))
            break;

          char ch = ' ';
          if (row < (int)data_.size() && cx < (int)data_[row].size())
            ch = data_[row][cx];
          renderBuf_.push_back(ch);
          ++x;
        }
      }
    }

    write_raw(renderBuf_.data(), renderBuf_.size());
    reset();

    // cursorPos_ is screen-data-relative; emit_cursor_ansi adds lnWidth_
    // offset.
    emit_cursor_ansi(cursorPos_[0], cursorPos_[1]);
    isChanged_ = false;
  }
};

// Clean up macros so they don't pollute other translation units
#undef BUF_COLOR
#undef BUF_BG
#undef BUF_FG
#undef WRITE_COLOR
#undef SCROLL_DEC
#undef SCROLL_INC