#include <context.hxx>
#include <cursor.hxx>
#include <display.hxx>
#include <input_handler.hxx>

#include <csignal>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// File loading
// ---------------------------------------------------------------------------

static std::vector<std::vector<char>> load_file(const std::string &path,
                                                int tab_size) {
  std::vector<std::vector<char>> out;
  std::ifstream in(path);
  if (!in.good())
    return out;

  std::string line;
  while (std::getline(in, line)) {
    std::vector<char> row;
    for (char c : line) {
      if (c == '\t') {
        int spaces = tab_size - (int)(row.size() % tab_size);
        for (int i = 0; i < spaces; ++i)
          row.push_back(' ');
      } else if (c != '\r') {
        row.push_back(c);
      }
    }
    out.push_back(std::move(row));
  }
  return out;
}

// ---------------------------------------------------------------------------
// File saving
// ---------------------------------------------------------------------------

static bool save_file(const std::string &path,
                      const std::vector<std::vector<char>> &data) {
  if (path.empty())
    return false;
  std::ofstream out(path, std::ios::trunc);
  if (!out.good())
    return false;
  for (const auto &row : data) {
    out.write(row.data(), (std::streamsize)row.size());
    out.put('\n');
  }
  return out.good();
}



static volatile std::sig_atomic_t g_run = 1;
static void on_sigint(int) { g_run = 0; }

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

static void cb_resize(AppContext *ctx, int w, int h) {
  ctx->display->notify_resize(w, h);
}

static void cb_key(AppContext *ctx, const Event &e) {
  Display &disp = *ctx->display;
  Cursor  &cur  = *ctx->cursor;
  auto pos = cur.get_pos(); // {row, col}, screen-data-relative, 1-based

  switch (e.key) {
  case Event::KeyCode::Character:
    disp.insert(pos[0], pos[1], e.ch);
    break;

  case Event::KeyCode::Enter:
    disp.newline();
    break;

  case Event::KeyCode::Backspace:
    disp.erase(pos[0], pos[1] - 1);
    break;

  case Event::KeyCode::Delete: {
    /* Delete key: erase the character *at* the cursor (col, not col-1). */
    auto dpos = cur.get_data_pos();
    auto &data = disp.get_data();
    if (dpos[0] < (int)data.size() && dpos[1] < (int)data[dpos[0]].size())
      disp.erase(pos[0], pos[1]);
    break;
  }

  case Event::KeyCode::Up:    cur.move_relative(Dir::UP,  1); break;
  case Event::KeyCode::Down:  cur.move_relative(Dir::BOT, 1); break;
  case Event::KeyCode::Left:  cur.move_relative(Dir::LFT, 1); break;
  case Event::KeyCode::Right: cur.move_relative(Dir::RGT, 1); break;

  case Event::KeyCode::PageUp:
    disp.scroll_up(disp.content_height());
    break;
  case Event::KeyCode::PageDown:
    disp.scroll_bot(disp.content_height());
    break;

  case Event::KeyCode::Home: cur.go_line_start(); break;
  case Event::KeyCode::End:  cur.go_line_end();   break;

  case Event::KeyCode::Tab:
    /* Expand tab to spaces at the current position. */
    for (int i = 0; i < ctx->tab_size; ++i) {
      pos = cur.get_pos();
      disp.insert(pos[0], pos[1], ' ');
    }
    break;

  case Event::KeyCode::Ctrl:
    if (e.ch == 'Q')
      *ctx->run = 0;
    else if (e.ch == 'S')
      save_file(ctx->filename, ctx->display->get_data());
    break;

  default:
    break;
  }
}

static void cb_mouse_scroll(AppContext *ctx, const Event &e) {
  if (e.delta > 0)
    ctx->display->scroll_up(e.delta);
  else
    ctx->display->scroll_bot(-e.delta);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, const char **argv) {
  /* Gather singletons. */
  Display      &disp  = Display::getInstance();
  Cursor       &cur   = Cursor::getInstance();
  InputHandler &input = InputHandler::getInstance();

  /* Build the application context. */
  AppContext ctx;
  ctx.display       = &disp;
  ctx.cursor        = &cur;
  ctx.input_handler = &input;
  ctx.run           = &g_run;
  ctx.tab_size      = 4;

  if (argc > 1)
    ctx.filename = argv[1];

  /* Load file (or start with an empty buffer). */
  std::vector<std::vector<char>> data;
  if (!ctx.filename.empty())
    data = load_file(ctx.filename, ctx.tab_size);
  if (data.empty())
    data.push_back({});   // always at least one line

  /* Configure display. */
  disp.set_line_numbering(false);
  disp.set_data(data);

  /* Configure cursor style. */
  cur.set_blink(CursorBlink::bar);

  /* Wire up input callbacks. */
  InputCallbacks cbs;
  cbs.ctx           = &ctx;
  cbs.onResize      = cb_resize;
  cbs.onKey         = cb_key;
  cbs.onMouseScroll = cb_mouse_scroll;
  input.set_callbacks(cbs);

  /* Start input polling and the main loop. */
  signal(SIGINT, on_sigint);
  input.start_poll();

  while (g_run) {
    input.pop();
    disp.render();
    usleep(8000); // ~60 fps
  }

  return 0;
}
