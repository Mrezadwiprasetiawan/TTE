#include <csignal>
#include <display.hxx>
#include <input_handler.hxx>
#include <string>
#include <unistd.h>

const std::string default_data =
    R"(Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed non risus. Suspendisse lectus tortor, dignissim sit amet, adipiscing nec, ultricies sed, dolor. Cras elementum ultrices diam. Maecenas ligula massa, varius a, semper congue, euismod non, mi.
Proin porttitor, orci nec nonummy molestie, enim est eleifend mi, non fermentum diam nisl sit amet erat. Duis semper. Duis arcu massa, scelerisque vitae, consequat in, pretium a, enim. Pellentesque congue.
Ut in risus volutpat libero pharetra tempor. Cras vestibulum bibendum augue. Praesent egestas leo in pede. Praesent blandit odio eu enim. Pellentesque sed dui ut augue blandit sodales.
Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia curae; Aliquam nibh. Mauris ac mauris sed pede pellentesque fermentum. Maecenas adipiscing ante non diam sodales hendrerit.
Ut velit mauris, egestas sed, gravida nec, ornare ut, mi. Aenean ut orci vel massa suscipit pulvinar. Nulla sollicitudin. Fusce varius, ligula non tempus aliquam, nunc turpis ullamcorper nibh, in tempus sapien eros vitae ligula.
Pellentesque rhoncus nunc et augue. Integer id felis. Curabitur aliquet pellentesque diam. Integer quis metus vitae elit lobortis egestas.
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Morbi vel erat non mauris convallis vehicula. Nulla et sapien. Integer tortor tellus, aliquam faucibus, convallis id, congue eu, quam. Mauris ullamcorper felis vitae erat.
Proin feugiat, augue non elementum posuere, metus purus iaculis lectus, et tristique ligula justo vitae magna. Aliquam convallis sollicitudin purus. Praesent aliquam, enim at fermentum mollis, ligula massa adipiscing nisl, ac euismod nibh nisl eu lectus.
Fusce vulputate sem at sapien. Vivamus leo. Aliquam euismod libero eu enim. Nulla nec felis sed leo placerat imperdiet. Aenean suscipit nulla in justo.
Suspendisse cursus rutrum augue. Nulla tincidunt tincidunt mi. Curabitur iaculis, lorem vel rhoncus faucibus, felis magna fermentum augue, et ultricies lacus lorem varius purus. Curabitur eu amet.)";

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
  Display &disp = Display::getInstance();
  InputHandler &inHdl = InputHandler::getInstance();
  InputCallbacks cbs;

  cbs.onResize = [&disp](int w, int h) {
    disp.notify_resize(w, h);
    disp.mark_changed();
  };

  cbs.onKey = [&disp](Event e) {
    switch (e.key) {
    default:
      return;

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
      disp.mark_changed();
      break;
    case Event::KeyCode::PageDown:
      disp.scroll_bot(disp.get_height());
      disp.mark_changed();
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
      disp.scroll_up(3);
    else
      disp.scroll_bot(3);
    disp.mark_changed();
  };

  signal(SIGINT, SIGINT_handler);
  inHdl.set_callbacks(cbs);
  inHdl.start_poll();
  disp.set_data(parse_raw(default_data, 2));
  disp.setCursorBlink(CursorBlink::underline);
  while (run) {
    inHdl.pop();
    disp.render();
    usleep(32);
  }
  disp.exitAlternateScreen();
  return 0;
}
