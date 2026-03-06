#pragma once

#include <csignal>
#include <string>

/*
 * Forward declarations only — full headers included by whoever needs them.
 * This keeps context.hxx cheap to include everywhere.
 */
class Display;
class Cursor;
class InputHandler;

/*
 * AppContext bundles every singleton and piece of app-level state into one
 * typed struct so callbacks receive a single, traceable pointer instead of
 * a bare void*.
 *
 * All pointer fields are non-owning — the singletons manage their own
 * lifetime.  AppContext is stack-allocated in main().
 */
struct AppContext {
  Display      *display       = nullptr;
  Cursor       *cursor        = nullptr;
  InputHandler *input_handler = nullptr;

  /* Allows any callback to stop the main loop cleanly. */
  volatile std::sig_atomic_t *run = nullptr;

  /* Path of the file currently open (empty = scratch buffer). */
  std::string filename;

  /* Spaces per tab when loading a file. */
  int tab_size = 4;
};
