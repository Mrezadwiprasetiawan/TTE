#include <csignal>
#include <display.hxx>
#include <fstream>
#include <input_handler.hxx>
#include <string>
#include <unistd.h>
#include <vector>

static std::vector<std::vector<char>> parse_raw(const std::string &s,
                                                int tabSize) {
  std::vector<std::vector<char>> out;
  std::vector<char> row;
  for (char c : s) {
    if (c == '\n') {
      out.push_back(row);
      row.clear();
    } else if (c == '\r')
      continue;
    else if (c == '\t') {
      int spaces = tabSize - (row.size() % tabSize);
      for (int i = 0; i < spaces; ++i)
        row.push_back(' ');
    } else
      row.push_back(c);
  }
  out.push_back(row);
  return out;
}

volatile std::sig_atomic_t run = 1;
void SIGINT_handler(int signal) { run = 0; }

int main(int argc, const char **argv) {
  using namespace std;
  vector<vector<char>> data;
  string filename = "";
  if (argc > 1)
    filename = string(argv[1]);
  if (!filename.empty()) {
    ifstream in(filename);
    if (in.good()) {
      string line;
      while (getline(in, line))
        data.emplace_back(line.begin(), line.end());
    }
  }
  Display &disp = Display::getInstance();
  InputHandler &inHdl = InputHandler::getInstance();
  InputCallbacks cbs;

  cbs.onResize = [&disp](int w, int h) {
    disp.notify_resize(w, h);
    disp.mark_changed();
  };

  cbs.onKey = [&disp](Event e) {
    switch (e.key) {
    default: {
      std::array<int, 2> pos = disp.get_cursor_pos();
      disp.insert(pos[0], pos[1], e.ch);
      break;
    }
    case Event::KeyCode::Backspace: {
      std::array<int, 2> pos = disp.get_cursor_pos();
      disp.erase(pos[0], pos[1] - 1);
      break;
    }

    case Event::KeyCode::Up:
      disp.move_cursor_relative(Dir::UP, 1);
      break;
    case Event::KeyCode::Down:
      disp.move_cursor_relative(Dir::BOT, 1);
      break;
    case Event::KeyCode::Left:
      disp.move_cursor_relative(Dir::LFT, 1);
      break;
    case Event::KeyCode::Right:
      disp.move_cursor_relative(Dir::RGT, 1);
      break;

    case Event::KeyCode::PageUp:
      disp.scroll_up(disp.get_height());
      break;
    case Event::KeyCode::PageDown:
      disp.scroll_bot(disp.get_height());
      break;

    case Event::KeyCode::Home:
      disp.go_line_start();
      break;
    case Event::KeyCode::End:
      disp.go_line_end();
      break;

    case Event::KeyCode::Ctrl:
      if (e.ch == 'Q')
        raise(SIGINT);
      break;
    }
  };

  cbs.onMouseScroll = [&disp](const Event &e) {
    if (e.delta > 0)
      disp.scroll_up(e.delta);
    else
      disp.scroll_bot(-e.delta);
  };

  signal(SIGINT, SIGINT_handler);
  inHdl.set_callbacks(cbs);
  inHdl.start_poll();
  disp.set_data(data);
  disp.setCursorBlink(CursorBlink::underline);
  while (run) {
    inHdl.pop();
    disp.render();
    usleep(16);
  }
  disp.exitAlternateScreen();
  return 0;
}
