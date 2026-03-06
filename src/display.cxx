#include <algorithm>
#include <cursor.hxx>  // also pulls in display.hxx

// Append a 24-bit ANSI colour escape into renderBuf
#define BUF_COLOR(layer, r, g, b)                                                        \
  do {                                                                                   \
    char _t[32];                                                                         \
    renderBuf.append(_t, snprintf(_t, 32, "\x1b[" layer ";2;%d;%d;%dm", (r), (g), (b))); \
  } while (0)
#define BUF_BG(r, g, b) BUF_COLOR("48", r, g, b)
#define BUF_FG(r, g, b) BUF_COLOR("38", r, g, b)

// Write a 24-bit ANSI colour escape directly to stdout
#define WRITE_COLOR(layer, r, g, b)                                               \
  do {                                                                            \
    char _t[32];                                                                  \
    write_raw(_t, snprintf(_t, 32, "\x1b[" layer ";2;%d;%d;%dm", (r), (g), (b))); \
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

bool Display::rgb_eq(const std::array<int, 3> &a, const std::array<int, 3> &b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

int Display::content_width() const { return lineNumbering ? width - lnWidth : width; }

int Display::content_height() const { return height; }

void Display::emit_cursor_ansi(int r, int c) {
  char buf[32];
  int  termCol = c + (lineNumbering ? lnWidth : 0);
  write_raw(buf, snprintf(buf, 32, "\x1b[%d;%dH", r, termCol));
}

void Display::show_cursor_raw() { write_raw("\x1b[?25h", 6); }

void Display::buf_line_number(int dataRow) {
  BUF_BG(lnBg[0], lnBg[1], lnBg[2]);
  BUF_FG(lnFg[0], lnFg[1], lnFg[2]);
  char tmp[16];
  renderBuf.append(tmp, snprintf(tmp, sizeof(tmp), "%*d ", lnWidth - 1, dataRow + 1));
  BUF_BG(mainBg[0],mainBg[1],mainBg[2]);
  BUF_FG(mainFg[0],mainFg[1],mainFg[2]);
}

void Display::update_extra_default_bg() {
  /* Pick a contrasting panel colour based on the perceived brightness of mainBg.
     Brightness uses the standard luma approximation (ITU-R BT.601). */
  int luma = (mainBg[0] * 299 + mainBg[1] * 587 + mainBg[2] * 114) / 1000;
  if (luma < 128) extraDefaultBg = {180, 180, 190};  // light panel on dark content
  else extraDefaultBg = {40, 42, 54};                // dark panel on light content
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
    width  = w.ws_col;
    height = w.ws_row;
  } else {
    width  = 80;
    height = 24;
  }
#else
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    width  = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  } else {
    width  = 80;
    height = 24;
  }
#endif

  renderBuf.reserve((size_t)width * height * 50);
  height -= extraHeight;
  dirtyRows.assign(content_height(), false);
  update_extra_default_bg();
  enterAlternateScreen();
}

Display &Display::getInstance() {
  static Display instance;
  return instance;
}

Display::~Display() {
  exitAlternateScreen();
#ifdef _WIN32
  if (hOut) SetConsoleMode(hOut, outModeOrig);
#endif
  show_cursor_raw();
}

// ---------------------------------------------------------------------------
// Data editing
// ---------------------------------------------------------------------------

void Display::insert(int row, int col, char c) {
  int reqRow = startRowData + row - 1, reqCol = startColData + col - 1;
  if (reqRow < 0 || reqRow >= (int)data.size()) return;
  data[reqRow].insert(data[reqRow].begin() + reqCol, c);
  mark_row_changed(row - 1);
  Cursor::getInstance().move_relative(Dir::RGT, 1);
}

void Display::erase(int row, int col) {
  int reqRow = startRowData + row - 1;
  if (reqRow < 0 || reqRow >= (int)data.size()) return;

  if (col == 0) {
    // Cursor is at the start of the line - merge with the line above
    if (reqRow == 0) return;
    int prevRow     = reqRow - 1;
    int junctionCol = (int)data[prevRow].size();
    data[prevRow].insert(data[prevRow].end(), data[reqRow].begin(), data[reqRow].end());
    data.erase(data.begin() + reqRow);
    // Line merge shifts everything below: needs full redraw
    mark_changed();
    int newScreenRow = prevRow - startRowData + 1;
    if (newScreenRow < 1) {
      scroll_up(1 - newScreenRow);
      newScreenRow = 1;
    }
    int newCol;
    int cw = content_width();
    if (junctionCol < startColData) {
      startColData = junctionCol;
      newCol       = 1;
    } else if (junctionCol - startColData + 1 > cw) {
      startColData = junctionCol - cw + 1;
      newCol       = cw;
    } else {
      newCol = junctionCol - startColData + 1;
    }
    Cursor::getInstance().move(newScreenRow, newCol);
  } else {
    // Erase the character before the cursor
    int reqCol = startColData + col - 1;
    if (reqCol < 0 || reqCol >= (int)data[reqRow].size()) return;
    data[reqRow].erase(data[reqRow].begin() + reqCol);
    if (data[reqRow].empty() && (int)data.size() > 1) {
      /* Row deleted entirely - all rows below shift up, needs full redraw */
      data.erase(data.begin() + reqRow);
      mark_changed();
    } else {
      mark_row_changed(row - 1);
    }
    Cursor::getInstance().move_relative(Dir::LFT, 1);
  }
}

void Display::replace(int row, int col, char c) {
  int reqRow = startRowData + row - 1;
  int reqCol = startColData + col - 1;
  if (reqRow < 0 || reqRow >= (int)data.size()) return;
  if (reqCol < 0 || reqCol >= (int)data[reqRow].size()) return;
  data[reqRow][reqCol] = c;
  mark_row_changed(row - 1);
}

void Display::insert_line(int row) {
  row = startRowData + row;
  if (row > (int)data.size()) row = (int)data.size();
  data.insert(data.begin() + row, std::vector<char>{});
  mark_changed();
}

void Display::newline() {
  Cursor &cur     = Cursor::getInstance();
  auto    cpos    = cur.get_pos();
  int     dataRow = startRowData + cpos[0] - 1;
  if (dataRow < 0 || dataRow >= (int)data.size()) return;

  int   dataCol = startColData + cpos[1] - 1;
  auto &curLine = data[dataRow];

  std::vector<char> newLine(dataCol < (int)curLine.size() ? curLine.begin() + dataCol : curLine.end(), curLine.end());
  if (dataCol < (int)curLine.size()) curLine.erase(curLine.begin() + dataCol, curLine.end());

  insert_line(cpos[0]);
  data[dataRow + 1] = std::move(newLine);

  startColData = 0;
  int newRow;
  if (cpos[0] < content_height()) {
    newRow = cpos[0] + 1;
  } else {
    scroll_bot(1);
    newRow = content_height();
  }
  cur.move(newRow, 1);
}

// ---------------------------------------------------------------------------
// Terminal control
// ---------------------------------------------------------------------------

void Display::enterAlternateScreen() {
  if (alt_screen) return;
  write_raw("\x1b[?1049h", 8);
  alt_screen = true;
}

void Display::exitAlternateScreen() {
  if (!alt_screen) return;
  write_raw("\x1b[?1049l", 8);
  alt_screen = false;
}

void Display::clear() { write_raw("\x1b[2J\x1b[H", 7); }

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------

bool Display::scroll_up(int dist) {
  if (startRowData == 0) {
    /* Already at the top — snap cursor to the first row. */
    Cursor::getInstance().move(1, Cursor::getInstance().get_pos()[1]);
    return false;
  }
  int actual    = std::min(dist, startRowData);
  startRowData -= actual;
  if (!isChanged) {
    if (scrollPending && scrollPendingDir == Dir::BOT) {
      // Opposite direction - fall back to full redraw
      isChanged         = true;
      scrollPending     = false;
      scrollPendingDist = 0;
      std::fill(dirtyRows.begin(), dirtyRows.end(), false);
    } else {
      /*
       * Accumulate scroll distance. Viewport shifted, so any previously
       * dirty row indices are now stale - clear them.
       */
      scrollPending      = true;
      scrollPendingDir   = Dir::UP;
      scrollPendingDist += actual;
      std::fill(dirtyRows.begin(), dirtyRows.end(), false);
      if (scrollPendingDist >= content_height()) {
        isChanged         = true;
        scrollPending     = false;
        scrollPendingDist = 0;
      }
    }
  }
  return true;
}

bool Display::scroll_bot(int dist) {
  int cap = (int)data.size() - content_height();
  if (cap < 0) cap = 0;
  if (startRowData >= cap) {
    /* Already at the bottom — snap cursor to the last visible data row. */
    int lastRow = std::min((int)data.size() - startRowData, content_height());
    Cursor::getInstance().move(lastRow, Cursor::getInstance().get_pos()[1]);
    return false;
  }
  int actual    = std::min(dist, cap - startRowData);
  startRowData += actual;
  if (!isChanged) {
    if (scrollPending && scrollPendingDir == Dir::UP) {
      // Opposite direction - fall back to full redraw
      isChanged         = true;
      scrollPending     = false;
      scrollPendingDist = 0;
      std::fill(dirtyRows.begin(), dirtyRows.end(), false);
    } else {
      /*
       * Accumulate scroll distance. Viewport shifted, so any previously
       * dirty row indices are now stale - clear them.
       */
      scrollPending      = true;
      scrollPendingDir   = Dir::BOT;
      scrollPendingDist += actual;
      std::fill(dirtyRows.begin(), dirtyRows.end(), false);
      if (scrollPendingDist >= content_height()) {
        isChanged         = true;
        scrollPending     = false;
        scrollPendingDist = 0;
      }
    }
  }
  return true;
}

bool Display::scroll_lft(int dist) {
  if (startColData == 0) return false;
  if (startColData >= dist) startColData -= dist;
  else startColData = 0;
  mark_changed();
  return true;
}

bool Display::scroll_rgt(int dist) {
  auto cpos = Cursor::getInstance().get_pos();
  if (startRowData + (cpos[0] - 1) >= (int)data.size()) return false;
  int row = startRowData + (cpos[0] - 1);
  int cw  = content_width();
  if (startColData + dist + cw <= (int)data[row].size() + 1) {
    startColData += dist;
    mark_changed();
    return true;
  }
  return false;
}

void Display::scroll_dir_to(Dir dir, int dist) {
  switch (dir) {
    case Dir::UP: scroll_up(dist); break;
    case Dir::BOT: scroll_bot(dist); break;
    case Dir::LFT: scroll_lft(dist); break;
    case Dir::RGT: scroll_rgt(dist); break;
  }
}

// ---------------------------------------------------------------------------
// Direct colour output
// ---------------------------------------------------------------------------

void Display::fgRGB(int r, int g, int b) { WRITE_COLOR("38", r, g, b); }
void Display::bgRGB(int r, int g, int b) { WRITE_COLOR("48", r, g, b); }
void Display::reset() { write_raw("\x1b[0m", 4); }

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void Display::notify_resize(int termW, int termH) {
  width        = termW;
  height       = termH - extraHeight;
  Cursor &cur  = Cursor::getInstance();
  auto    cpos = cur.get_pos();
  cur.move(std::min(cpos[0], content_height()), std::min(cpos[1], content_width()));
  dirtyRows.assign(content_height(), false);
  mark_changed();
}

// ---------------------------------------------------------------------------
// Getters - viewport and terminal geometry
// ---------------------------------------------------------------------------

int  Display::get_width() const { return width; }
int  Display::get_height() const { return height + extraHeight; }
int  Display::get_extra_height() const { return extraHeight; }
int  Display::get_start_row_data() const { return startRowData; }
int  Display::get_start_col_data() const { return startColData; }
bool Display::get_alt_screen() const { return alt_screen; }

// ---------------------------------------------------------------------------
// Getters / setters - line-number gutter
// ---------------------------------------------------------------------------

void Display::set_line_numbering(bool val) {
  lineNumbering = val;
  mark_changed();
}
bool Display::get_line_numbering() const { return lineNumbering; }

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
std::array<int, 3> Display::get_ln_bg() const { return lnBg; }
std::array<int, 3> Display::get_ln_fg() const { return lnFg; }

// ---------------------------------------------------------------------------
// Getters / setters - main content colours
// ---------------------------------------------------------------------------

void Display::set_main_bg(int r, int g, int b) {
  mainBg = {r, g, b};
  update_extra_default_bg();
  mark_changed();
}
void Display::set_main_fg(int r, int g, int b) {
  mainFg = {r, g, b};
  mark_changed();
}
std::array<int, 3> Display::get_main_bg() const { return mainBg; }
std::array<int, 3> Display::get_main_fg() const { return mainFg; }

// ---------------------------------------------------------------------------
// Getters / setters - content data
// ---------------------------------------------------------------------------

void Display::set_row_data(int row, const std::vector<char> &buf) {
  if (row < 0) return;
  if (row >= (int)data.size()) data.resize(row + 1);
  data[row] = buf;
  mark_changed();
}

void Display::add_data(const std::vector<std::vector<char>> &buf) {
  for (const auto &r : buf) data.push_back(r);
  mark_changed();
}

void Display::set_data(const std::vector<std::vector<char>> &d) {
  data = d;
  mark_changed();
}

std::vector<std::vector<char>>       &Display::get_data() { return data; }
const std::vector<std::vector<char>> &Display::get_data() const { return data; }

// ---------------------------------------------------------------------------
// Colour spans - content area
// ---------------------------------------------------------------------------

void Display::add_span(std::vector<ColorSpan> &spans, ColorSpan s) {
  std::vector<ColorSpan> result;
  result.reserve(spans.size() + 2);

  for (const auto &ex : spans) {
    if (ex.row != s.row) {
      result.push_back(ex);
      continue;
    }
    if (ex.start == s.start && ex.end == s.end) continue;
    if (ex.end < s.start || ex.start > s.end) {
      result.push_back(ex);
      continue;
    }
    if (ex.start < s.start) {
      ColorSpan l = ex;
      l.end       = s.start - 1;
      result.push_back(l);
    }
    if (ex.end > s.end) {
      ColorSpan r = ex;
      r.start     = s.end + 1;
      result.push_back(r);
    }
  }

  result.push_back(s);
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
const std::vector<ColorSpan> &Display::get_bg_spans() const { return bg; }
const std::vector<ColorSpan> &Display::get_fg_spans() const { return fg; }

// ---------------------------------------------------------------------------
// Getters / setters - pinned extra area
// ---------------------------------------------------------------------------

void Display::set_extra(const std::array<std::vector<char>, extraHeight> &rows) {
  extraData    = rows;
  extraSet     = true;
  extraChanged = true;
}

void Display::set_extra_bg_span(ColorSpan s) {
  add_span(extraBg, s);
  extraChanged = true;
}
void Display::set_extra_fg_span(ColorSpan s) {
  add_span(extraFg, s);
  extraChanged = true;
}
void Display::clear_extra_spans() {
  extraBg.clear();
  extraFg.clear();
  extraChanged = true;
}

bool Display::get_extra_set() const { return extraSet; }
bool Display::get_extra_changed() const { return extraChanged; }

const std::array<std::vector<char>, Display::extraHeight> &Display::get_extra_data() const { return extraData; }

const std::vector<ColorSpan> &Display::get_extra_bg_spans() const { return extraBg; }
const std::vector<ColorSpan> &Display::get_extra_fg_spans() const { return extraFg; }

void Display::set_extra_default_bg(int r, int g, int b) {
  extraDefaultBg = {r, g, b};
  extraChanged   = true;
}
std::array<int, 3> Display::get_extra_default_bg() const { return extraDefaultBg; }

// ---------------------------------------------------------------------------
// Dirty / change state
// ---------------------------------------------------------------------------

bool                     Display::get_is_changed() const { return isChanged; }
bool                     Display::get_scroll_pending() const { return scrollPending; }
Dir                      Display::get_scroll_pending_dir() const { return scrollPendingDir; }
int                      Display::get_scroll_pending_dist() const { return scrollPendingDist; }
const std::vector<bool> &Display::get_dirty_rows() const { return dirtyRows; }

// ---------------------------------------------------------------------------
// Render control
// ---------------------------------------------------------------------------

void Display::mark_changed() {
  isChanged         = true;
  scrollPending     = false;
  scrollPendingDist = 0;
  std::fill(dirtyRows.begin(), dirtyRows.end(), false);
  // extraChanged is intentionally NOT cleared here:
  // the full redraw path in render() will repaint extra rows too, then clear it.
}

void Display::mark_row_changed(int screenY) {
  if (isChanged || screenY < 0 || screenY >= (int)dirtyRows.size()) return;
  dirtyRows[screenY] = true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void Display::render_line(int y) {
  int  row = startRowData + y;
  char pos[32];
  renderBuf.append(pos, snprintf(pos, 32, "\x1b[%d;1H\x1b[2K", y + 1));

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
  int       x  = 0;
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
      int  cx  = startColData + x;
      bool ib  = (bgSpan.row == row && x >= bgSpan.start && x <= bgSpan.end);
      bool iff = (fgSpan.row == row && x >= fgSpan.start && x <= fgSpan.end);
      if (!rgb_eq(ib ? bgSpan.RGB : mainBg, wantBg)) break;
      if (!rgb_eq(iff ? fgSpan.RGB : mainFg, wantFg)) break;

      char ch = ' ';
      if (row < (int)data.size() && cx < (int)data[row].size()) ch = data[row][cx];
      renderBuf.push_back(ch);
      ++x;
    }
  }
}

void Display::render_extra_line(int extraY) {
  int  termRow = content_height() + 1 + extraY;
  char pos[32];
  renderBuf.append(pos, snprintf(pos, 32, "\x1b[%d;1H\x1b[2K", termRow));

  if (!extraSet) {
    if (!rgb_eq(extraDefaultBg, renderCurBg)) {
      buf_bg(extraDefaultBg);
      renderCurBg = extraDefaultBg;
    }
    for (int x = 0; x < width; ++x) renderBuf.push_back(' ');
    return;
  }

  const std::vector<char> &rowData = extraData[extraY];

  ColorSpan bgSpan{-1, 0, 0, extraDefaultBg};
  ColorSpan fgSpan{-1, 0, 0, mainFg};
  for (auto &b : extraBg)
    if (b.row == extraY) {
      bgSpan = b;
      break;
    }
  for (auto &f : extraFg)
    if (f.row == extraY) {
      fgSpan = f;
      break;
    }

  std::array<int, 3> baseBg = extraDefaultBg;
  std::array<int, 3> baseFg = mainFg;

  int x = 0;
  while (x < width) {
    bool inBg = (bgSpan.row == extraY && x >= bgSpan.start && x <= bgSpan.end);
    bool inFg = (fgSpan.row == extraY && x >= fgSpan.start && x <= fgSpan.end);

    std::array<int, 3> wantBg = inBg ? bgSpan.RGB : baseBg;
    std::array<int, 3> wantFg = inFg ? fgSpan.RGB : baseFg;

    if (!rgb_eq(wantBg, renderCurBg)) {
      buf_bg(wantBg);
      renderCurBg = wantBg;
    }
    if (!rgb_eq(wantFg, renderCurFg)) {
      buf_fg(wantFg);
      renderCurFg = wantFg;
    }

    while (x < width) {
      bool ib  = (bgSpan.row == extraY && x >= bgSpan.start && x <= bgSpan.end);
      bool iff = (fgSpan.row == extraY && x >= fgSpan.start && x <= fgSpan.end);
      if (!rgb_eq(ib ? bgSpan.RGB : baseBg, wantBg)) break;
      if (!rgb_eq(iff ? fgSpan.RGB : baseFg, wantFg)) break;

      char ch = ' ';
      if (x < (int)rowData.size()) ch = rowData[x];
      renderBuf.push_back(ch);
      ++x;
    }
  }
}

void Display::render() {
  const int ch   = content_height();
  auto      cpos = Cursor::getInstance().get_pos();

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

    for (int y = 0; y < ch; ++y) render_line(y);

    for (int e = 0; e < extraHeight; ++e) render_extra_line(e);

    char pos[32];
    int  termCol = cpos[1] + (lineNumbering ? lnWidth : 0);
    renderBuf.append(pos, snprintf(pos, 32, "\x1b[%d;%dH", cpos[0], termCol));
    renderBuf.append("\x1b[?25h", 6);

    write_raw(renderBuf.data(), renderBuf.size());
    isChanged         = false;
    extraChanged      = false;
    scrollPending     = false;
    scrollPendingDist = 0;

  } else if (scrollPending) {
    renderBuf.clear();
    renderCurBg = {-1, -1, -1};
    renderCurFg = {-1, -1, -1};

    int  dist = scrollPendingDist;
    char esc[64];
    if (scrollPendingDir == Dir::UP) {
      /* Scroll region restricted to 1..ch so extra rows are never disturbed. */
      renderBuf.append(esc, snprintf(esc, 64, "\x1b[1;%dr\x1b[%dT\x1b[r", ch, dist));
      for (int y = 0; y < dist; ++y) render_line(y);
    } else {
      renderBuf.append(esc, snprintf(esc, 64, "\x1b[1;%dr\x1b[%dS\x1b[r", ch, dist));
      for (int y = ch - dist; y < ch; ++y) render_line(y);
    }

    if (extraChanged) {
      for (int e = 0; e < extraHeight; ++e) render_extra_line(e);
      extraChanged = false;
    }

    char pos[32];
    int  termCol = cpos[1] + (lineNumbering ? lnWidth : 0);
    renderBuf.append(pos, snprintf(pos, 32, "\x1b[%d;%dH", cpos[0], termCol));

    write_raw(renderBuf.data(), renderBuf.size());
    scrollPending     = false;
    scrollPendingDist = 0;

  } else if (std::any_of(dirtyRows.begin(), dirtyRows.end(), [](bool b) { return b; }) || extraChanged) {
    /*
     * Partial redraw: only repaint rows flagged dirty (content and/or extra).
     * No screen clear - cursor positioning targets each row individually.
     */
    renderBuf.clear();
    renderBuf.append("\x1b[?25l", 6);
    renderCurBg = {-1, -1, -1};
    renderCurFg = {-1, -1, -1};

    for (int y = 0; y < ch; ++y)
      if (dirtyRows[y]) render_line(y);

    if (extraChanged) {
      for (int e = 0; e < extraHeight; ++e) render_extra_line(e);
      extraChanged = false;
    }

    char pos[32];
    int  termCol = cpos[1] + (lineNumbering ? lnWidth : 0);
    renderBuf.append(pos, snprintf(pos, 32, "\x1b[%d;%dH", cpos[0], termCol));
    renderBuf.append("\x1b[?25h", 6);

    write_raw(renderBuf.data(), renderBuf.size());
    std::fill(dirtyRows.begin(), dirtyRows.end(), false);

  } else {
    emit_cursor_ansi(cpos[0], cpos[1]);
  }
}

// Clean up macros so they don't pollute other translation units
#undef BUF_COLOR
#undef BUF_BG
#undef BUF_FG
#undef WRITE_COLOR
