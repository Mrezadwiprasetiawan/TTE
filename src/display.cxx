#include <display.hxx>

//  Macros (undef'd at bottom of file)
// Append ANSI 24-bit colour escape into renderBuf_
#define BUF_COLOR(layer, r, g, b)                                              \
  do {                                                                         \
    char _t[32];                                                               \
    renderBuf.append(                                                          \
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

void Display::write_raw(const char *s, size_t n) {
#ifndef _WIN32
  ::write(STDOUT_FILENO, s, n);
#else
  DWORD w;
  WriteConsoleA(hOut, s, (DWORD)n, &w, nullptr);
#endif
}

void Display::write_raw(const std::string &s) { write_raw(s.data(), s.size()); }

void Display::buf_bg(const std::array<int, 3> &c) { BUF_BG(c[0], c[1], c[2]); }
void Display::buf_fg(const std::array<int, 3> &c) { BUF_FG(c[0], c[1], c[2]); }

bool Display::rgb_eq(const std::array<int, 3> &a, const std::array<int, 3> &b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

int Display::content_width() const {
  return lineNumbering ? width - lnWidth : width;
}

// Length of the data row currently under the cursor
int Display::cur_row_len() const {
  int dr = startRowData + (cursorPos[0] - 1);
  return (dr < (int)data.size()) ? (int)data[dr].size() : 0;
}

void Display::clamp_col_to_row() {
  int len = cur_row_len();
  int maxCol = (len > startColData)
                   ? std::min(content_width() + 1, len - startColData + 1)
                   : 1;
  cursorPos[1] = std::max(1, std::min(cursorPos[1], maxCol));
}

void Display::emit_cursor_ansi(int r, int c) {
  char buf[32];
  int termCol = c + (lineNumbering ? lnWidth : 0);
  write_raw(buf, snprintf(buf, 32, "\x1b[%d;%dH", r, termCol));
}

void Display::buf_line_number(int dataRow) {
  BUF_BG(lnBg[0], lnBg[1], lnBg[2]);
  BUF_FG(lnFg[0], lnFg[1], lnFg[2]);
  char tmp[16];
  renderBuf.append(
      tmp, snprintf(tmp, sizeof(tmp), "%*d ", lnWidth - 1, dataRow + 1));
}

Display::Display() {
#ifdef _WIN32
  hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    GetConsoleMode(hOut, &outModeOrig);
    SetConsoleMode(hOut, outModeOrig | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#endif

#ifndef _WIN32
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    width = w.ws_col;
    height = w.ws_row;
  } else {
    width = 80;
    height = 24;
  }
#else
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  } else {
    width = 80;
    height = 24;
  }
#endif

  renderBuf.reserve((size_t)width * height * 50);
  height -= extraHeight;
  enterAlternateScreen();
}

Display &Display::getInstance() {
  static Display instance;
  return instance;
}

Display::~Display() {
  exitAlternateScreen();
#ifdef _WIN32
  if (hOut)
    SetConsoleMode(hOut, outModeOrig);
#endif
  showCursor();
}

void Display::insert(int row, int col, char c) {
  int reqRow = startRowData + row - 1, reqCol = startColData + col - 1;
  if (reqRow < 0 || reqRow >= (int)data.size())
    return;
  data[reqRow].insert(data[reqRow].begin() + reqCol, c);
  mark_changed();
  move_cursor_relative(Dir::RGT, 1);
}

void Display::erase(int row, int col) {
  int reqRow = startRowData + row - 1;
  if (reqRow < 0 || reqRow >= (int)data.size())
    return;

  if (col == 0) {
    // Cursor at start of line
    if (reqRow == 0)
      return; // First line: nothing to do
    // Merge current line into previous (handles empty line too)
    int prevRow = reqRow - 1;
    int junctionCol = (int)data[prevRow].size();
    data[prevRow].insert(data[prevRow].end(), data[reqRow].begin(),
                         data[reqRow].end());
    data.erase(data.begin() + reqRow);
    mark_changed();
    // Place cursor at the junction point
    int newScreenRow = prevRow - startRowData + 1;
    if (newScreenRow < 1) {
      scroll_up(1 - newScreenRow);
      newScreenRow = 1;
    }
    cursorPos[0] = newScreenRow;
    int cw = content_width();
    if (junctionCol < startColData) {
      startColData = junctionCol;
      cursorPos[1] = 1;
    } else if (junctionCol - startColData + 1 > cw) {
      startColData = junctionCol - cw + 1;
      cursorPos[1] = cw;
    } else {
      cursorPos[1] = junctionCol - startColData + 1;
    }
  } else {
    // Erase character before cursor
    int reqCol = startColData + col - 1;
    if (reqCol < 0 || reqCol >= (int)data[reqRow].size())
      return;
    data[reqRow].erase(data[reqRow].begin() + reqCol);
    // Delete empty line (keep at least one line)
    if (data[reqRow].empty() && (int)data.size() > 1)
      data.erase(data.begin() + reqRow);
    mark_changed();
    move_cursor_relative(Dir::LFT, 1);
  }
}
void Display::insert_line(int row) {
  row = startRowData + row;
  if (row > data.size())
    row = data.size();
  std::vector<char> empty_line;
  data.insert(data.begin() + row, empty_line);
}
void Display::newline() {
  int dataRow = startRowData + cursorPos[0] - 1;
  if (dataRow < 0 || dataRow >= (int)data.size())
    return;

  int dataCol = startColData + cursorPos[1] - 1;
  auto &curLine = data[dataRow];

  std::vector<char> newLine(
      dataCol < (int)curLine.size() ? curLine.begin() + dataCol : curLine.end(),
      curLine.end());
  if (dataCol < (int)curLine.size())
    curLine.erase(curLine.begin() + dataCol, curLine.end());

  insert_line(cursorPos[0]); // insert at dataRow+1
  data[dataRow + 1] = std::move(newLine);
  mark_changed();

  startColData = 0;
  if (cursorPos[0] < height)
    cursorPos[0] += 1;
  else
    scroll_bot(1);
  cursorPos[1] = 1;
}

void Display::enterAlternateScreen() {
  if (alt_screen)
    return;
  write_raw("\x1b[?1049h", 8);
  alt_screen = true;
}

void Display::exitAlternateScreen() {
  if (!alt_screen)
    return;
  write_raw("\x1b[?1049l", 8);
  alt_screen = false;
}

void Display::clear() { write_raw("\x1b[2J\x1b[H", 7); }
void Display::hideCursor() { write_raw("\x1b[?25l", 6); }
void Display::showCursor() { write_raw("\x1b[?25h", 6); }

void Display::setCursorBlink(CursorBlink b) {
  char buf[16];
  write_raw(buf, snprintf(buf, 16, "\x1b[%d q", (int)b));
}

// move_cursor: update internal screen-data position AND move terminal cursor.
// (r, c) are screen-data-relative (1-based).
void Display::move_cursor(int r, int c) { cursorPos = {r, c}; }
void Display::move_cursor(std::array<int, 2> pos) {
  move_cursor(pos[0], pos[1]);
}

// move_cursor_relative: move the cursor by `dist` in direction `dir`,
// scrolling the viewport when the cursor would leave the visible area.
// cursorPos stays screen-data-relative throughout; the terminal cursor is
void Display::move_cursor_relative(Dir dir, int dist) {
  if (dist <= 0)
    return;
  switch (dir) {
  case Dir::UP: {
    if (cursorPos[0] - dist >= 1) {
      // Cursor stays on screen
      cursorPos[0] -= dist;
    } else {
      // Hit the top of the screen — scroll the viewport
      int rem = dist - (cursorPos[0] - 1);
      cursorPos[0] = 1;
      scroll_up(rem);
    }
    clamp_col_to_row();
    break;
  }
  case Dir::BOT: {
    int target = startRowData + (cursorPos[0] - 1) + dist;
    if (target >= (int)data.size())
      break; // Don't go past the last data row
    if (cursorPos[0] + dist <= height) {
      // Cursor stays on screen
      cursorPos[0] += dist;
    } else {
      // Hit the bottom of the screen — scroll the viewport
      int over = (cursorPos[0] + dist) - height;
      cursorPos[0] = height;
      scroll_bot(over);
    }
    clamp_col_to_row();
    break;
  }
  case Dir::RGT: {
    int cw = content_width();
    int maxCol = std::min(cw, cur_row_len() - startColData);
    if (cursorPos[1] + dist <= maxCol + 1) {
      // Cursor stays on screen
      cursorPos[1] += dist;
    } else if (cursorPos[1] <= maxCol) {
      // Clamp to end of visible row content
      cursorPos[1] = maxCol;
    } else if (scroll_rgt(dist)) {
      // Scrolled right — keep cursor within new visible content
      cursorPos[1] =
          std::min(cursorPos[1], std::min(cw, cur_row_len() - startColData));
    }
    break;
  }
  case Dir::LFT: {
    if (cursorPos[1] - dist >= 1) {
      // Cursor stays on screen
      cursorPos[1] -= dist;
    } else {
      // Hit the left edge — scroll the viewport
      int rem = dist - (cursorPos[1] - 1);
      cursorPos[1] = 1;
      scroll_lft(rem);
    }
    break;
  }
  }
}

// Returns {dataRow, dataCol} (0-based) of the cursor in the full data buffer.
std::array<int, 2> Display::get_cursor_data_pos() {
  return {cursorPos[0] + startRowData - 1, cursorPos[1] + startColData - 1};
}

bool Display::scroll_up(int dist) {
  if (startRowData == 0)
    return false;
  int actual = std::min(dist, startRowData);
  startRowData -= actual;
  if (!isChanged) {
    if (scrollPending && scrollPendingDir == Dir::BOT) {
      // Opposite direction — fall back to full redraw
      isChanged = true;
      scrollPending = false;
      scrollPendingDist = 0;
    } else {
      scrollPending = true;
      scrollPendingDir = Dir::UP;
      scrollPendingDist += actual;
      if (scrollPendingDist >= height) {
        isChanged = true;
        scrollPending = false;
        scrollPendingDist = 0;
      }
    }
  }
  return true;
}

bool Display::scroll_bot(int dist) {
  int cap = (int)data.size() - height;
  if (cap < 0)
    cap = 0;
  if (startRowData >= cap)
    return false;
  int actual = std::min(dist, cap - startRowData);
  startRowData += actual;
  if (!isChanged) {
    if (scrollPending && scrollPendingDir == Dir::UP) {
      isChanged = true;
      scrollPending = false;
      scrollPendingDist = 0;
    } else {
      scrollPending = true;
      scrollPendingDir = Dir::BOT;
      scrollPendingDist += actual;
      if (scrollPendingDist >= height) {
        isChanged = true;
        scrollPending = false;
        scrollPendingDist = 0;
      }
    }
  }
  return true;
}

void Display::scroll_dir_to(Dir dir, int dist) {
  switch (dir) {
  case Dir::UP:
    scroll_up(dist);
    break;
  case Dir::BOT:
    scroll_bot(dist);
    break;
  case Dir::LFT:
    scroll_lft(dist);
    break;
  case Dir::RGT:
    scroll_rgt(dist);
    break;
  }
}

// Move cursor to the first column of the current line.
void Display::go_line_start() {
  startColData = 0;
  cursorPos[1] = 1;
}

// Move cursor to the last character of the current line,
// scrolling horizontally so it is visible.
void Display::go_line_end() {
  int dataRow = get_cursor_data_pos()[0];
  if (dataRow >= (int)data.size())
    return;
  int len = (int)data[dataRow].size();
  int cw = content_width();
  if (len == 0) {
    startColData = 0;
    cursorPos[1] = 1;
  } else if (len <= cw) {
    startColData = 0;
    cursorPos[1] = len + 1;
  } else {
    startColData = len - cw + 1;
    cursorPos[1] = cw + 1;
  }
}

bool Display::scroll_rgt(int dist) {
  if (startRowData + (cursorPos[0] - 1) >= (int)data.size())
    return false;
  int row = startRowData + (cursorPos[0] - 1);
  int cw = content_width();
  if (startColData + dist + cw <= (int)data[row].size() + 1) {
    startColData += dist;
    return true;
  }
  return false;
}

void Display::fgRGB(int r, int g, int b) { WRITE_COLOR("38", r, g, b); }
void Display::bgRGB(int r, int g, int b) { WRITE_COLOR("48", r, g, b); }
void Display::reset() { write_raw("\x1b[0m", 4); }

void Display::notify_resize(int termW, int termH) {
  width = termW;
  height = termH - extraHeight;
  // Keep cursor within new screen bounds
  cursorPos[0] = std::min(cursorPos[0], height);
  cursorPos[1] = std::min(cursorPos[1], content_width());
  mark_changed();
}

int Display::get_width() const { return content_width(); }
int Display::get_height() const { return height; }
std::array<int, 2> Display::get_cursor_pos() const { return cursorPos; }

void Display::set_line_numbering(bool val) {
  lineNumbering = val;
  mark_changed();
}

bool Display::get_line_numbering_state() const { return lineNumbering; }

void Display::set_ln_width(int w) {
  lnWidth = w;
  mark_changed();
}

int Display::get_ln_width() const { return lnWidth; }

void Display::set_ln_colors(std::array<int, 3> bg, std::array<int, 3> fg) {
  lnBg = bg;
  lnFg = fg;
  mark_changed();
}

void Display::set_row_data(int row, const std::vector<char> &buf) {
  if (row < 0)
    return;
  if (row >= (int)data.size())
    data.resize(row + 1);
  data[row] = buf;
  mark_changed();
}

void Display::add_data(const std::vector<std::vector<char>> &buf) {
  for (const auto &r : buf)
    data.push_back(r);
  mark_changed();
}

void Display::set_data(const std::vector<std::vector<char>> &d) {
  data = d;
  mark_changed();
}

std::vector<std::vector<char>> &Display::get_data() { return data; }
const std::vector<std::vector<char>> &Display::get_data() const { return data; }

// Add a span while ensuring the new span is always fully visible.
//
// Rules (applied per existing span on the same row):
//   1. Identical (row, start, end)  → replace in-place (color swapped).
//   2. Overlap                      → trim the existing span so the new one
//                                     is never obscured; if an existing span
//                                     fully contains the new one it is split
//                                     into a left and right remnant.
//   3. No overlap / different row   → keep as-is.
void Display::add_span(std::vector<ColorSpan> &spans, ColorSpan s) {
  std::vector<ColorSpan> result;
  result.reserve(spans.size() + 2); // at most one split = +2

  for (const auto &ex : spans) {
    // Different row → untouched
    if (ex.row != s.row) {
      result.push_back(ex);
      continue;
    }

    // Identical location → drop existing; new span replaces it entirely
    if (ex.start == s.start && ex.end == s.end)
      continue;

    // No overlap → keep existing
    if (ex.end < s.start || ex.start > s.end) {
      result.push_back(ex);
      continue;
    }

    // Overlapping: keep only the parts of `ex` that lie outside `s`

    // Left remnant  [ex.start .. s.start-1]
    if (ex.start < s.start) {
      ColorSpan left = ex;
      left.end = s.start - 1;
      result.push_back(left);
    }

    // Right remnant [s.end+1 .. ex.end]
    if (ex.end > s.end) {
      ColorSpan right = ex;
      right.start = s.end + 1;
      result.push_back(right);
    }
    // The portion of `ex` covered by `s` is intentionally discarded
  }

  result.push_back(s); // new span always appended last (highest priority)
  spans = std::move(result);
}

void Display::set_bg_span(ColorSpan s) {
  add_span(bg, s);
  mark_changed();
}

void Display::set_fg_span(ColorSpan s) {
  add_span(fg, s);
  mark_changed();
}

void Display::clear_color_spans() {
  bg.clear();
  fg.clear();
  mark_changed();
}

void Display::set_main_bg(int r, int g, int b) {
  mainBg = {r, g, b};
  mark_changed();
}

void Display::set_main_fg(int r, int g, int b) {
  mainFg = {r, g, b};
  mark_changed();
}

void Display::mark_changed() {
  isChanged = true;
  scrollPending = false;
  scrollPendingDist = 0;
}

void Display::render() {
  if (isChanged) {
    renderBuf.clear();
    renderBuf.append("\x1b[?25l", 6);
    renderBuf.append("\x1b[2J\x1b[H", 7);

    renderCurBg = {-1, -1, -1};
    renderCurFg = {-1, -1, -1};
    buf_bg(mainBg);
    renderCurBg = mainBg;
    buf_fg(mainFg);
    renderCurFg = mainFg;

    for (int y = 0; y < height; ++y)
      render_line(y);

    char pos[32];
    int termCol = cursorPos[1] + (lineNumbering ? lnWidth : 0);
    renderBuf.append(pos,
                     snprintf(pos, 32, "\x1b[%d;%dH", cursorPos[0], termCol));
    renderBuf.append("\x1b[?25h", 6);

    write_raw(renderBuf.data(), renderBuf.size());
    isChanged = false;
    scrollPending = false;
    scrollPendingDist = 0;
  } else if (scrollPending) {
    renderBuf.clear();
    renderCurBg = {-1, -1, -1};
    renderCurFg = {-1, -1, -1};

    int dist = scrollPendingDist;
    char esc[64];
    if (scrollPendingDir == Dir::UP) {
      // Viewport scrolled up: content shifts down, render new top lines
      renderBuf.append(
          esc, snprintf(esc, 64, "\x1b[1;%dr\x1b[%dT\x1b[r", height, dist));
      for (int y = 0; y < dist; ++y)
        render_line(y);
    } else {
      // Viewport scrolled down: content shifts up, render new bottom lines
      renderBuf.append(
          esc, snprintf(esc, 64, "\x1b[1;%dr\x1b[%dS\x1b[r", height, dist));
      for (int y = height - dist; y < height; ++y)
        render_line(y);
    }

    char pos[32];
    int termCol = cursorPos[1] + (lineNumbering ? lnWidth : 0);
    renderBuf.append(pos,
                     snprintf(pos, 32, "\x1b[%d;%dH", cursorPos[0], termCol));

    write_raw(renderBuf.data(), renderBuf.size());
    scrollPending = false;
    scrollPendingDist = 0;
  } else {
    emit_cursor_ansi(cursorPos[0], cursorPos[1]);
  }
}

void Display::render_line(int y) {
  int row = startRowData + y;
  char pos[32];
  renderBuf.append(pos, snprintf(pos, 32, "\x1b[%d;1H", y + 1));

  if (lineNumbering) {
    buf_line_number(row);
    if (!rgb_eq(mainBg, renderCurBg)) {
      buf_bg(mainBg);
      renderCurBg = mainBg;
    }
    if (!rgb_eq(mainFg, renderCurFg)) {
      buf_fg(mainFg);
      renderCurFg = mainFg;
    }
  }

  ColorSpan bgSpan{-1, 0, 0, mainBg};
  ColorSpan fgSpan{-1, 0, 0, mainFg};
  for (auto &b : bg)
    if (b.row == row) {
      bgSpan = b;
      break;
    }
  for (auto &f : fg)
    if (f.row == row) {
      fgSpan = f;
      break;
    }

  const int cw = content_width();
  int x = 0;
  while (x < cw) {
    bool inBg = (bgSpan.row == row && x >= bgSpan.start && x <= bgSpan.end);
    bool inFg = (fgSpan.row == row && x >= fgSpan.start && x <= fgSpan.end);

    std::array<int, 3> wantBg = inBg ? bgSpan.RGB : mainBg;
    std::array<int, 3> wantFg = inFg ? fgSpan.RGB : mainFg;

    if (!rgb_eq(wantBg, renderCurBg)) {
      buf_bg(wantBg);
      renderCurBg = wantBg;
    }
    if (!rgb_eq(wantFg, renderCurFg)) {
      buf_fg(wantFg);
      renderCurFg = wantFg;
    }

    while (x < cw) {
      int cx = startColData + x;
      bool ib = (bgSpan.row == row && x >= bgSpan.start && x <= bgSpan.end);
      bool iff = (fgSpan.row == row && x >= fgSpan.start && x <= fgSpan.end);
      if (!rgb_eq(ib ? bgSpan.RGB : mainBg, wantBg))
        break;
      if (!rgb_eq(iff ? fgSpan.RGB : mainFg, wantFg))
        break;

      char ch = ' ';
      if (row < (int)data.size() && cx < (int)data[row].size())
        ch = data[row][cx];
      renderBuf.push_back(ch);
      ++x;
    }
  }
}

// Clean up macros so they don't pollute other translation units
#undef BUF_COLOR
#undef BUF_BG
#undef BUF_FG
#undef WRITE_COLOR
