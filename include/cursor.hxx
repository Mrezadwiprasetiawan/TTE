#pragma once

#include <array>
#include <display.hxx> // CursorBlink, Dir, Display

class Cursor {

  /*
   * pos is screen-data-relative, 1-based.
   * [0] = row  : 1 = top visible content line
   * [1] = col  : 1 = leftmost visible data column
   * Terminal column = pos[1] + lnWidth (resolved in Display::render).
   */
  std::array<int, 2> pos = {1, 1};
  CursorBlink blinkStyle = CursorBlink::def;

  /* Returns the character length of the data row under the cursor. */
  int cur_row_len() const;

  /* Clamps pos[1] so it cannot exceed the last valid column of the current row. */
  void clamp_col_to_row();

  Cursor() = default;

public:
  Cursor(const Cursor &) = delete;
  Cursor &operator=(const Cursor &) = delete;

  static Cursor &getInstance();

  // -----------------------------------------------------------------------
  // Position
  // -----------------------------------------------------------------------

  /* Set cursor to an absolute screen-data position (1-based). */
  void move(int r, int c);
  void move(std::array<int, 2> p);

  /*
   * Move by dist in direction dir. Scrolls the Display viewport when the
   * cursor would leave the visible content area.
   */
  void move_relative(Dir dir, int dist);

  /* Screen-data-relative position (1-based). */
  std::array<int, 2> get_pos() const;

  /* Full data-buffer position {dataRow, dataCol}, 0-based. */
  std::array<int, 2> get_data_pos() const;

  // -----------------------------------------------------------------------
  // Line navigation
  // -----------------------------------------------------------------------

  /* Jump to column 1 of the current line and reset the horizontal viewport. */
  void go_line_start();

  /* Jump to the last character of the current line, scrolling horizontally
     into view if needed. */
  void go_line_end();

  // -----------------------------------------------------------------------
  // Visibility and style
  // -----------------------------------------------------------------------

  void hide();
  void show();

  void set_blink(CursorBlink b);
  CursorBlink get_blink() const;
};
