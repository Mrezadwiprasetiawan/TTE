#pragma once

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
#define FUNC_EVENT(name) (*name)(void*,const Event &)
  void (*onResize)(void*,int, int), FUNC_EVENT(onKey), FUNC_EVENT(onMousePress),
      FUNC_EVENT(onMouseRelease), FUNC_EVENT(onMouseMove),
      FUNC_EVENT(onMouseScroll), FUNC_EVENT(onUnhandled);
  void *ctx;
};

class InputHandler {
  InputHandler();

  void push(const Event &e);

  InputCallbacks cbs;
  std::queue<Event> back_queue, present_queue;
  std::mutex mutex;
  bool polling = false;
  std::thread poll_thread;

#ifndef _WIN32
  inline static volatile bool winch_flag = false;
  static void handle_winch(int);
  termios orig{};
#else
  HANDLE hIn = nullptr;
  DWORD inModeOrig = 0;
#endif

  void enable_raw();
  void disable_raw();
  void write_raw(const char *s, size_t n);
  void enable_mouse();
  void disable_mouse();
  void dispatch(const Event &e);
  void poll_loop();

public:
  InputHandler(const InputHandler &) = delete;
  InputHandler &operator=(const InputHandler &) = delete;

  static InputHandler &getInstance();
  ~InputHandler();

  void pop();
  void start_poll();
  void stop_poll();
  void set_callbacks(InputCallbacks cbs);
};
