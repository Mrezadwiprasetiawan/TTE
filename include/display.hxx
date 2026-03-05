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

// Generate scroll member functions (horizontal only — vertical uses terminal
// scroll regions)
#define SCROLL_DEC(name, field)                                                \
  bool scroll_##name(int dist) {                                               \
    if (field >= dist) {                                                       \
      field -= dist;                                                           \
    } else if (field) {                                                        \
      field = 0;                                                               \
    } else {                                                                   \
      return false;                                                            \
    }                                                                          \
    mark_changed();                                                            \
    return true;                                                               \
  }

class Display {

  void write_raw(const char *s, size_t n);
  void write_raw(const std::string &s);

  void buf_bg(const std::array<int, 3> &c);
  void buf_fg(const std::array<int, 3> &c);

  static bool rgb_eq(const std::array<int, 3> &a, const std::array<int, 3> &b);

  int content_width() const;
  int cur_row_len() const;
  void clamp_col_to_row();
  void emit_cursor_ansi(int r, int c);
  void buf_line_number(int dataRow);
  void render_line(int y);

  Display();
  bool alt_screen = false;

#ifdef _WIN32
  HANDLE hOut = nullptr;
  DWORD outModeOrig = 0;
#endif

  int width, height;
  int startRowData = 0, startColData = 0;

  // Line-number gutter
  bool lineNumbering = true;
  int lnWidth = 5; // digits + 1 trailing space
  std::array<int, 3> lnBg = {0, 0, 0};
  std::array<int, 3> lnFg = {255, 255, 255};

  const int extraHeight = 3;

  // cursorPos: screen-data-relative, 1-based.
  // Row 1 = top visible line, Col 1 = leftmost visible data column.
  // Terminal column = cursorPos[1] + lnWidth (handled in emit_cursor_ansi).
  std::array<int, 2> cursorPos = {1, 1};
  bool isChanged = false;

  std::vector<std::vector<char>> data;
  std::array<int, 3> mainBg = {0, 0, 0};
  std::array<int, 3> mainFg = {255, 255, 255};
  std::vector<ColorSpan> bg, fg;

  std::string renderBuf;
  std::array<int, 3> renderCurBg = {-1, -1, -1};
  std::array<int, 3> renderCurFg = {-1, -1, -1};

  // Incremental scroll state
  bool scrollPending = false;
  Dir scrollPendingDir = Dir::UP;
  int scrollPendingDist = 0;

public:
  Display(const Display &) = delete;
  Display &operator=(const Display &) = delete;

  static Display &getInstance();

  ~Display();

  void insert(int row, int col, char c);
  void erase(int row, int col);
  void insert_line(int row);
  void newline();

  void enterAlternateScreen();
  void exitAlternateScreen();

  void clear();
  void hideCursor();
  void showCursor();

  void setCursorBlink(CursorBlink b);

  // move_cursor: update internal screen-data position AND move terminal cursor.
  // (r, c) are screen-data-relative (1-based).
  void move_cursor(int r, int c);
  void move_cursor(std::array<int, 2> pos);

  // move_cursor_relative: move the cursor by `dist` in direction `dir`,
  // scrolling the viewport when the cursor would leave the visible area.
  // cursorPos stays screen-data-relative throughout; the terminal cursor is
  void move_cursor_relative(Dir dir, int dist);

  // Returns {dataRow, dataCol} (0-based) of the cursor in the full data buffer.
  std::array<int, 2> get_cursor_data_pos();

  void scroll_dir_to(Dir dir, int dist);

  // Move cursor to the first column of the current line.
  void go_line_start();

  // Move cursor to the last character of the current line,
  // scrolling horizontally so it is visible.
  void go_line_end();

  SCROLL_DEC(lft, startColData)

  bool scroll_up(int dist);
  bool scroll_bot(int dist);
  bool scroll_rgt(int dist);

  void fgRGB(int r, int g, int b);
  void bgRGB(int r, int g, int b);
  void reset();

  void notify_resize(int termW, int termH);

  int get_width() const;
  int get_height() const;
  std::array<int, 2> get_cursor_pos() const;

  void set_line_numbering(bool val);
  bool get_line_numbering_state() const;

  void set_ln_width(int w);
  int get_ln_width() const;

  void set_ln_colors(std::array<int, 3> bg, std::array<int, 3> fg);

  void set_row_data(int row, const std::vector<char> &buf);
  void add_data(const std::vector<std::vector<char>> &buf);
  void set_data(const std::vector<std::vector<char>> &d);

  std::vector<std::vector<char>> &get_data();
  const std::vector<std::vector<char>> &get_data() const;

  // Add a span while ensuring the new span is always fully visible.
  //
  // Rules (applied per existing span on the same row):
  //   1. Identical (row, start, end)  → replace in-place (color swapped).
  //   2. Overlap                      → trim the existing span so the new one
  //                                     is never obscured; if an existing span
  //                                     fully contains the new one it is split
  //                                     into a left and right remnant.
  //   3. No overlap / different row   → keep as-is.
  static void add_span(std::vector<ColorSpan> &spans, ColorSpan s);

  void set_bg_span(ColorSpan s);
  void set_fg_span(ColorSpan s);
  void clear_color_spans();

  void set_main_bg(int r, int g, int b);
  void set_main_fg(int r, int g, int b);

  void mark_changed();

  void render();
};

// Clean up macros so they don't pollute other translation units
#undef SCROLL_DEC
