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

class Display {

  void write_raw(const char *s, size_t n);
  void write_raw(const std::string &s);

  void buf_bg(const std::array<int, 3> &c);
  void buf_fg(const std::array<int, 3> &c);

  static bool rgb_eq(const std::array<int, 3> &a, const std::array<int, 3> &b);

  int cur_row_len() const;
  void clamp_col_to_row();
  void emit_cursor_ansi(int r, int c);
  void buf_line_number(int dataRow);
  void render_line(int y);

  /*
   * Renders one row of the pinned extra area (extraY is 0-based).
   * Honours extraBg / extraFg colour spans (row index = extraY).
   * When no content has been set, fills the row with extraDefaultBg.
   */
  void render_extra_line(int extraY);

  /* Marks a single content row (0-based screen-y) dirty for partial re-render. */
  void mark_row_changed(int screenY);

  /* Recomputes extraDefaultBg to contrast against the current mainBg. */
  void update_extra_default_bg();

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

  /*
   * Number of rows pinned at the bottom for the extra area.
   * Must equal the array size used in set_extra().
   */
  static constexpr int extraHeight = 2;

  /*
   * cursorPos is screen-data-relative, 1-based.
   * Row 1 = top visible line, Col 1 = leftmost visible data column.
   * Terminal column = cursorPos[1] + lnWidth (handled in emit_cursor_ansi).
   */
  std::array<int, 2> cursorPos = {1, 1};
  bool isChanged = false;

  /*
   * Per-content-row dirty flags (0-based, size = content_height()).
   * render() only redraws flagged rows when neither isChanged nor scrollPending.
   */
  std::vector<bool> dirtyRows;

  std::vector<std::vector<char>> data;
  std::array<int, 3> mainBg = {0, 0, 0};
  std::array<int, 3> mainFg = {255, 255, 255};
  std::vector<ColorSpan> bg, fg;

  /*
   * Pinned extra area at the bottom of the terminal.
   *   extraSet        - caller has provided content via set_extra().
   *   extraChanged    - only the extra rows need a redraw (not the content area).
   *   extraDefaultBg  - contrasting fill used when extraSet is false.
   *   extraBg/extraFg - colour spans; span.row is 0 or 1 (index into extra rows).
   */
  bool extraSet = false;
  bool extraChanged = false;
  std::array<std::vector<char>, extraHeight> extraData;
  std::vector<ColorSpan> extraBg, extraFg;
  std::array<int, 3> extraDefaultBg = {180, 180, 190};

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

  // -----------------------------------------------------------------------
  // Content dimensions
  // -----------------------------------------------------------------------

  /* Columns available for content (terminal width minus line-number gutter). */
  int content_width() const;

  /*
   * Rows available for scrollable content (terminal height minus extra rows).
   * All scroll and cursor logic uses this rather than the raw height field.
   */
  int content_height() const;

  // -----------------------------------------------------------------------
  // Data editing
  // -----------------------------------------------------------------------

  void insert(int row, int col, char c);
  void erase(int row, int col);
  /* Overwrites the character at (row, col) in-place without shifting neighbours.
     Only the affected screen row is re-rendered. */
  void replace(int row, int col, char c);
  void insert_line(int row);
  void newline();

  // -----------------------------------------------------------------------
  // Terminal control
  // -----------------------------------------------------------------------

  void enterAlternateScreen();
  void exitAlternateScreen();
  void clear();
  void hideCursor();
  void showCursor();
  void setCursorBlink(CursorBlink b);

  // -----------------------------------------------------------------------
  // Cursor movement
  // -----------------------------------------------------------------------

  // Moves the cursor; (r, c) are screen-data-relative, 1-based.
  void move_cursor(int r, int c);
  void move_cursor(std::array<int, 2> pos);

  /* Moves the cursor by dist in direction dir, scrolling the viewport
     when the cursor would leave the visible area. */
  void move_cursor_relative(Dir dir, int dist);

  // Returns {dataRow, dataCol} (0-based) of the cursor in the full data buffer.
  std::array<int, 2> get_cursor_data_pos();

  void go_line_start();
  void go_line_end();

  // -----------------------------------------------------------------------
  // Scrolling
  // -----------------------------------------------------------------------

  void scroll_dir_to(Dir dir, int dist);
  bool scroll_up(int dist);
  bool scroll_bot(int dist);
  bool scroll_lft(int dist);
  bool scroll_rgt(int dist);

  // -----------------------------------------------------------------------
  // Direct colour output (bypasses buffer; use outside render loop)
  // -----------------------------------------------------------------------

  void fgRGB(int r, int g, int b);
  void bgRGB(int r, int g, int b);
  void reset();

  // -----------------------------------------------------------------------
  // Resize
  // -----------------------------------------------------------------------

  void notify_resize(int termW, int termH);

  // -----------------------------------------------------------------------
  // Getters - viewport and terminal geometry
  // -----------------------------------------------------------------------

  int get_width() const;           // raw terminal columns
  int get_height() const;          // raw terminal rows (content + extra)
  int get_extra_height() const;    // always extraHeight (2)
  int get_start_row_data() const;  // current vertical viewport offset (0-based)
  int get_start_col_data() const;  // current horizontal viewport offset (0-based)
  bool get_alt_screen() const;
  std::array<int, 2> get_cursor_pos() const; // screen-data-relative, 1-based

  // -----------------------------------------------------------------------
  // Getters / setters - line-number gutter
  // -----------------------------------------------------------------------

  void set_line_numbering(bool val);
  bool get_line_numbering() const;

  void set_ln_width(int w);
  int get_ln_width() const;

  void set_ln_colors(std::array<int, 3> bg, std::array<int, 3> fg);
  std::array<int, 3> get_ln_bg() const;
  std::array<int, 3> get_ln_fg() const;

  // -----------------------------------------------------------------------
  // Getters / setters - main content colours
  // -----------------------------------------------------------------------

  void set_main_bg(int r, int g, int b);
  void set_main_fg(int r, int g, int b);
  std::array<int, 3> get_main_bg() const;
  std::array<int, 3> get_main_fg() const;

  // -----------------------------------------------------------------------
  // Getters / setters - content data
  // -----------------------------------------------------------------------

  void set_row_data(int row, const std::vector<char> &buf);
  void add_data(const std::vector<std::vector<char>> &buf);
  void set_data(const std::vector<std::vector<char>> &d);
  std::vector<std::vector<char>> &get_data();
  const std::vector<std::vector<char>> &get_data() const;

  // -----------------------------------------------------------------------
  // Colour spans - content area
  // -----------------------------------------------------------------------

  /*
   * Inserts a ColorSpan into `spans`, always keeping the new span fully visible.
   *   - Same (row, start, end) as existing -> replaces it in-place.
   *   - Overlapping existing -> trims/splits the existing span around the new one.
   *   - No overlap / different row -> existing span kept as-is.
   */
  static void add_span(std::vector<ColorSpan> &spans, ColorSpan s);

  void set_bg_span(ColorSpan s);
  void set_fg_span(ColorSpan s);
  void clear_color_spans();
  const std::vector<ColorSpan> &get_bg_spans() const;
  const std::vector<ColorSpan> &get_fg_spans() const;

  // -----------------------------------------------------------------------
  // Getters / setters - pinned extra area
  // -----------------------------------------------------------------------

  /*
   * Sets the content of the pinned extra rows and schedules a redraw.
   * rows[0] -> first extra row  (terminal row content_height() + 1)
   * rows[1] -> second extra row (terminal row content_height() + 2)
   *
   * Colour spans are set via set_extra_bg_span / set_extra_fg_span, where
   * ColorSpan::row is 0 or 1 (index into the extra rows, not data rows).
   */
  void set_extra(const std::array<std::vector<char>, extraHeight> &rows);
  void set_extra_bg_span(ColorSpan s);
  void set_extra_fg_span(ColorSpan s);
  void clear_extra_spans();

  bool get_extra_set() const;
  bool get_extra_changed() const;
  const std::array<std::vector<char>, extraHeight> &get_extra_data() const;
  const std::vector<ColorSpan> &get_extra_bg_spans() const;
  const std::vector<ColorSpan> &get_extra_fg_spans() const;

  /*
   * The contrasting background used when extraSet is false.
   * Auto-derived from mainBg, but can be overridden.
   */
  void set_extra_default_bg(int r, int g, int b);
  std::array<int, 3> get_extra_default_bg() const;

  // -----------------------------------------------------------------------
  // Dirty / change state (read-only; mutate via mark_changed)
  // -----------------------------------------------------------------------

  bool get_is_changed() const;
  bool get_scroll_pending() const;
  Dir  get_scroll_pending_dir() const;
  int  get_scroll_pending_dist() const;
  const std::vector<bool> &get_dirty_rows() const;

  // -----------------------------------------------------------------------
  // Render control
  // -----------------------------------------------------------------------

  void mark_changed();
  void render();
};
