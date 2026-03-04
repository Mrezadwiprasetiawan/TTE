#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <signal.h>
#include <thread>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

enum class Dir { UP, BOT, RGT, LFT };

struct Event {
  enum class Type {
    None,
    Key,
    MousePress,
    MouseRelease,
    MouseMove,
    MouseScroll,
    Resize
  } type = Type::None;

  enum class KeyCode {
    None,
    Character,
    Ctrl,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Backspace,
    Delete,
    Escape,
    Tab,
    Insert,
    Home,
    End,
    PageUp,
    PageDown
  } key = KeyCode::None;

  char ch = 0;
  int x = 0, y = 0;
  int button = 0;
  int delta = 0;
  int w = 0, h = 0;
};

struct InputCallbacks {
  std::function<void(int w, int h)> onResize;
  std::function<void(const Event &)> onKey;
  std::function<void(const Event &)> onMousePress;
  std::function<void(const Event &)> onMouseRelease;
  std::function<void(const Event &)> onMouseMove;
  std::function<void(const Event &)> onMouseScroll;
  std::function<void(const Event &)> onUnhandled;
};

class InputHandler {
  InputHandler() {
#ifndef _WIN32
    signal(SIGWINCH, handle_winch);
    tcgetattr(STDIN_FILENO, &orig_);
#else
    hIn_ = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn_ != INVALID_HANDLE_VALUE)
      GetConsoleMode(hIn_, &inModeOrig_);
#endif
    enable_raw();
    enable_mouse();
  }

  void push(const Event &e) {
    std::lock_guard<std::mutex> lock(mutex_);
    back_queue_.push(e);
  }

  InputCallbacks callbacks_;

  std::queue<Event> back_queue_;
  std::queue<Event> present_queue_;
  std::mutex mutex_;

  bool polling_ = false;
  std::thread poll_thread_;

#ifndef _WIN32
  inline static volatile bool winch_flag_ = false;
  static void handle_winch(int) { winch_flag_ = true; }
  termios orig_{};
#else
  HANDLE hIn_ = nullptr;
  DWORD inModeOrig_ = 0;
#endif

  void enable_raw() {
#ifndef _WIN32
    termios t = orig_;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_oflag &= ~(OPOST);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
#else
    if (!hIn_)
      return;
    DWORD m = inModeOrig_;
    m &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    m |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hIn_, m);
#endif
  }

  void disable_raw() {
#ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
#else
    if (hIn_)
      SetConsoleMode(hIn_, inModeOrig_);
#endif
  }

  void write_raw(const char *s, size_t n) {
#ifndef _WIN32
    ::write(STDOUT_FILENO, s, n);
#else
    DWORD w;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)n, &w, nullptr);
#endif
  }

  void enable_mouse() {
    write_raw("\x1b[?1003h", 8);
    write_raw("\x1b[?1006h", 8);
  }
  void disable_mouse() {
    write_raw("\x1b[?1003l", 8);
    write_raw("\x1b[?1006l", 8);
  }

  void dispatch(const Event &e) {
    switch (e.type) {
    case Event::Type::Resize:
      if (callbacks_.onResize)
        callbacks_.onResize(e.w, e.h);
      break;
    case Event::Type::Key:
      if (callbacks_.onKey)
        callbacks_.onKey(e);
      break;
    case Event::Type::MousePress:
      if (callbacks_.onMousePress)
        callbacks_.onMousePress(e);
      break;
    case Event::Type::MouseRelease:
      if (callbacks_.onMouseRelease)
        callbacks_.onMouseRelease(e);
      break;
    case Event::Type::MouseMove:
      if (callbacks_.onMouseMove)
        callbacks_.onMouseMove(e);
      break;
    case Event::Type::MouseScroll:
      if (callbacks_.onMouseScroll)
        callbacks_.onMouseScroll(e);
      break;
    default:
      if (callbacks_.onUnhandled)
        callbacks_.onUnhandled(e);
      break;
    }
  }

  void poll_loop() {
    while (true) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!polling_)
          break;
      }

#ifndef _WIN32
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);
      timeval tv{0, 1000};
      int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);

      if (winch_flag_) {
        winch_flag_ = false;
        struct winsize ws;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        Event e;
        e.type = Event::Type::Resize;
        e.w = ws.ws_col;
        e.h = ws.ws_row;
        push(e);
      }

      if (ready <= 0)
        continue;

      char buf[256];
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n <= 0)
        continue;

      for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == 27) {
          // Mouse: ESC [ < b ; x ; y M/m
          if (i + 2 < n && buf[i + 1] == '[' && buf[i + 2] == '<') {
            i += 3;
            int b = 0, x = 0, y = 0;
            while (i < n && buf[i] != ';')
              b = b * 10 + (buf[i++] - '0');
            ++i;
            while (i < n && buf[i] != ';')
              x = x * 10 + (buf[i++] - '0');
            ++i;
            while (i < n && buf[i] != 'M' && buf[i] != 'm')
              y = y * 10 + (buf[i++] - '0');
            if (i >= n)
              break;
            bool press = (buf[i] == 'M');
            Event e;
            e.x = x;
            e.y = y;
            if (b & 64) {
              e.type = Event::Type::MouseScroll;
              e.delta = (b & 1) ? -1 : 1;
            } else if (b & 32) {
              e.type = Event::Type::MouseMove;
              e.button = b & 3;
            } else {
              e.type =
                  press ? Event::Type::MousePress : Event::Type::MouseRelease;
              e.button = b & 3;
            }
            push(e);
            continue;
          }
          // CSI: ESC [ ...
          if (i + 1 < n && buf[i + 1] == '[' && i + 2 < n) {
            i += 2;
            Event e;
            e.type = Event::Type::Key;
            bool handled = true;
            switch (buf[i]) {
            case 'A':
              e.key = Event::KeyCode::Up;
              break;
            case 'B':
              e.key = Event::KeyCode::Down;
              break;
            case 'C':
              e.key = Event::KeyCode::Right;
              break;
            case 'D':
              e.key = Event::KeyCode::Left;
              break;
            case 'H':
              e.key = Event::KeyCode::Home;
              break;
            case 'F':
              e.key = Event::KeyCode::End;
              break;
            case '1':
              if (i + 1 < n && buf[i + 1] == '~') {
                e.key = Event::KeyCode::Home;
                ++i;
              } else
                handled = false;
              break;
            case '2':
              if (i + 1 < n && buf[i + 1] == '~') {
                e.key = Event::KeyCode::Insert;
                ++i;
              } else
                handled = false;
              break;
            case '3':
              if (i + 1 < n && buf[i + 1] == '~') {
                e.key = Event::KeyCode::Delete;
                ++i;
              } else
                handled = false;
              break;
            case '4':
              if (i + 1 < n && buf[i + 1] == '~') {
                e.key = Event::KeyCode::End;
                ++i;
              } else
                handled = false;
              break;
            case '5':
              if (i + 1 < n && buf[i + 1] == '~') {
                e.key = Event::KeyCode::PageUp;
                ++i;
              } else
                handled = false;
              break;
            case '6':
              if (i + 1 < n && buf[i + 1] == '~') {
                e.key = Event::KeyCode::PageDown;
                ++i;
              } else
                handled = false;
              break;
            default:
              handled = false;
              break;
            }
            if (handled)
              push(e);
            continue;
          }
          // Bare ESC
          {
            Event e;
            e.type = Event::Type::Key;
            e.key = Event::KeyCode::Escape;
            push(e);
          }
        } else {
          Event e;
          e.type = Event::Type::Key;
          unsigned char c = static_cast<unsigned char>(buf[i]);
          if (c >= 1 && c <= 26) {
            e.key = Event::KeyCode::Ctrl;
            e.ch = static_cast<char>('A' + c - 1);
          } else if (c == 127) {
            e.key = Event::KeyCode::Backspace;
          } else if (c == '\r') {
            e.key = Event::KeyCode::Enter;
          } else if (c == '\t') {
            e.key = Event::KeyCode::Tab;
          } else {
            e.key = Event::KeyCode::Character;
            e.ch = static_cast<char>(c);
          }
          push(e);
        }
      }

#else // _WIN32
      INPUT_RECORD rec[32];
      DWORD n = 0;
      if (!PeekConsoleInput(hIn_, rec, 32, &n) || !n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      ReadConsoleInput(hIn_, rec, n, &n);
      for (DWORD i = 0; i < n; ++i) {
        auto &r = rec[i];
        if (r.EventType == WINDOW_BUFFER_SIZE_EVENT) {
          Event e;
          e.type = Event::Type::Resize;
          e.w = r.Event.WindowBufferSizeEvent.dwSize.X;
          e.h = r.Event.WindowBufferSizeEvent.dwSize.Y;
          push(e);
        } else if (r.EventType == KEY_EVENT && r.Event.KeyEvent.bKeyDown) {
          Event e;
          e.type = Event::Type::Key;
          WORD vk = r.Event.KeyEvent.wVirtualKeyCode;
          char ch = r.Event.KeyEvent.uChar.AsciiChar;
          if (vk == VK_UP)
            e.key = Event::KeyCode::Up;
          else if (vk == VK_DOWN)
            e.key = Event::KeyCode::Down;
          else if (vk == VK_LEFT)
            e.key = Event::KeyCode::Left;
          else if (vk == VK_RIGHT)
            e.key = Event::KeyCode::Right;
          else if (vk == VK_RETURN)
            e.key = Event::KeyCode::Enter;
          else if (vk == VK_BACK)
            e.key = Event::KeyCode::Backspace;
          else if (vk == VK_DELETE)
            e.key = Event::KeyCode::Delete;
          else if (vk == VK_HOME)
            e.key = Event::KeyCode::Home;
          else if (vk == VK_END)
            e.key = Event::KeyCode::End;
          else if (vk == VK_PRIOR)
            e.key = Event::KeyCode::PageUp;
          else if (vk == VK_NEXT)
            e.key = Event::KeyCode::PageDown;
          else if (vk == VK_INSERT)
            e.key = Event::KeyCode::Insert;
          else if (vk == VK_ESCAPE)
            e.key = Event::KeyCode::Escape;
          else if (vk == VK_TAB)
            e.key = Event::KeyCode::Tab;
          else if (ch >= 1 && ch <= 26) {
            e.key = Event::KeyCode::Ctrl;
            e.ch = 'A' + ch - 1;
          } else {
            e.key = Event::KeyCode::Character;
            e.ch = ch;
          }
          push(e);
        }
      }
#endif
    }
  }

public:
  InputHandler(const InputHandler &) = delete;
  InputHandler &operator=(const InputHandler &) = delete;

  static InputHandler &getInstance() {
    static InputHandler instance;
    return instance;
  }

  ~InputHandler() {
    stop_poll();
    disable_mouse();
    disable_raw();
  }

  void pop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::swap(present_queue_, back_queue_);
    }
    while (!present_queue_.empty()) {
      dispatch(present_queue_.front());
      present_queue_.pop();
    }
  }

  void start_poll() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      polling_ = true;
    }
    poll_thread_ = std::thread(&InputHandler::poll_loop, this);
  }

  void stop_poll() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      polling_ = false;
    }
    if (poll_thread_.joinable())
      poll_thread_.join();
  }

  void set_callbacks(InputCallbacks cbs) { callbacks_ = std::move(cbs); }
};