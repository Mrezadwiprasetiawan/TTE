#include <cursor.hxx>

#ifndef _WIN32
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void write_stdout(const char *s, int n) {
#ifndef _WIN32
  ::write(STDOUT_FILENO, s, n);
#else
  // On Windows, Display already enables VT processing; write via stdout fd.
  ::_write(1, s, n);
#endif
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

Cursor &Cursor::getInstance() {
  static Cursor instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Private helpers (need Display internals via friend)
// ---------------------------------------------------------------------------

int Cursor::cur_row_len() const {
  Display &d = Display::getInstance();
  int dr = d.startRowData + (pos[0] - 1);
  return (dr < (int)d.data.size()) ? (int)d.data[dr].size() : 0;
}

void Cursor::clamp_col_to_row() {
  Display &d = Display::getInstance();
  int len = cur_row_len();
  int maxCol = (len > d.startColData)
                   ? std::min(d.content_width() + 1, len - d.startColData + 1)
                   : 1;
  pos[1] = std::max(1, std::min(pos[1], maxCol));
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------

void Cursor::move(int r, int c) { pos = {r, c}; }
void Cursor::move(std::array<int, 2> p) { pos = p; }

void Cursor::move_relative(Dir dir, int dist) {
  if (dist <= 0)
    return;

  Display &d = Display::getInstance();

  switch (dir) {
  case Dir::UP: {
    if (pos[0] - dist >= 1) {
      pos[0] -= dist;
    } else {
      int rem = dist - (pos[0] - 1);
      pos[0] = 1;
      d.scroll_up(rem);
    }
    clamp_col_to_row();
    break;
  }
  case Dir::BOT: {
    int target = d.startRowData + (pos[0] - 1) + dist;
    if (target >= (int)d.data.size())
      break;
    if (pos[0] + dist <= d.content_height()) {
      pos[0] += dist;
    } else {
      int over = (pos[0] + dist) - d.content_height();
      pos[0] = d.content_height();
      d.scroll_bot(over);
    }
    clamp_col_to_row();
    break;
  }
  case Dir::RGT: {
    int cw = d.content_width();
    int maxCol = std::min(cw, cur_row_len() - d.startColData);
    if (pos[1] + dist <= maxCol + 1) {
      pos[1] += dist;
    } else if (pos[1] <= maxCol) {
      pos[1] = maxCol;
    } else if (d.scroll_rgt(dist)) {
      pos[1] = std::min(pos[1], std::min(cw, cur_row_len() - d.startColData));
    }
    break;
  }
  case Dir::LFT: {
    if (pos[1] - dist >= 1) {
      pos[1] -= dist;
    } else {
      int rem = dist - (pos[1] - 1);
      pos[1] = 1;
      d.scroll_lft(rem);
    }
    break;
  }
  }
}

std::array<int, 2> Cursor::get_pos() const { return pos; }

std::array<int, 2> Cursor::get_data_pos() const {
  Display &d = Display::getInstance();
  return {pos[0] + d.startRowData - 1, pos[1] + d.startColData - 1};
}

// ---------------------------------------------------------------------------
// Line navigation
// ---------------------------------------------------------------------------

void Cursor::go_line_start() {
  Display &d = Display::getInstance();
  bool scrolled = d.startColData != 0;
  d.startColData = 0;
  pos[1] = 1;
  if (scrolled)
    d.mark_changed();
}

void Cursor::go_line_end() {
  Display &d = Display::getInstance();
  int dataRow = get_data_pos()[0];
  if (dataRow >= (int)d.data.size())
    return;
  int len = (int)d.data[dataRow].size();
  int cw = d.content_width();
  int prevColData = d.startColData;
  if (len == 0) {
    d.startColData = 0;
    pos[1] = 1;
  } else if (len <= cw) {
    d.startColData = 0;
    pos[1] = len + 1;
  } else {
    d.startColData = len - cw + 1;
    pos[1] = cw + 1;
  }
  if (d.startColData != prevColData)
    d.mark_changed();
}

// ---------------------------------------------------------------------------
// Visibility and style
// ---------------------------------------------------------------------------

void Cursor::hide() { write_stdout("\x1b[?25l", 6); }
void Cursor::show() { write_stdout("\x1b[?25h", 6); }

void Cursor::set_blink(CursorBlink b) {
  blinkStyle = b;
  char buf[16];
  int n = snprintf(buf, sizeof(buf), "\x1b[%d q", (int)b);
  write_stdout(buf, n);
}

CursorBlink Cursor::get_blink() const { return blinkStyle; }
