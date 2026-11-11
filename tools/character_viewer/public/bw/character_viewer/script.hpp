#ifndef BW_CHARACTER_VIEWER_SCRIPT_HPP
#define BW_CHARACTER_VIEWER_SCRIPT_HPP

#include "bw/platform/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: scripted input for character_viewer: a text file whose
// lines become the same WindowEvents the window produces, plus a dump
// command, so a test or an agent drives the viewer with no hand on the
// mouse. The parse is a pure function of the text; applying the commands
// is the viewer's (main.cpp), through the one handler the window's events
// reach. An OS adapter's own conversions are tested apart
// (bw/platform/os/macos/window.hpp).
//
// Grammar. One command per line; lines end in '\n', with a '\r' before it
// dropped, so a file written on Windows parses the same; words separated
// by spaces or tabs; a blank line or one whose first word starts with '#'
// is skipped. Numbers
// are decimal, X and Y integers, LINES a finite float with no exponent,
// each spelled by the whole word. A button named twice in one move counts
// once.
//   move X Y [left] [right] [middle]   MouseMoved at (X, Y) with the named
//                                      buttons held
//   button left|right|middle down|up X Y
//                                      MouseButton pressed (down) or
//                                      released (up) at (X, Y)
//   scroll LINES                       Scrolled by LINES
//   key NAME [down|up]                 Key pressed (down, the default) or
//                                      released; NAME one of Escape, R, P,
//                                      W, A, S, D, Left, Right, Up, Down,
//                                      Equal, Minus, LeftBracket,
//                                      RightBracket, Other
//   dump FILE                          the frame this command is applied to
//                                      is written to FILE, the rest of the
//                                      line after the command word
//
// Contract.
//   1. parse_script(text) returns the commands in line order; an Event
//      command's `event` is the constructor's value for the line
//      (window.hpp, the four input constructors); a Dump command's `file`
//      is the line's remainder with no leading or trailing blanks.
//   2. The first line in error ends the parse: UnknownCommand (a first
//      word not in the grammar), ArgumentCount (a word count the command
//      does not take), InvalidNumber (a number word not spelled as above,
//      or an integer outside int32), UnknownKey, UnknownButton (a name not
//      in its list, including down|up misspelled), EmptyDumpFile. `line`
//      is the 1-based line and `word` the offending word, viewing `text`;
//      the commands are then not meaningful.
//   3. Applied by the viewer, one command per drawn frame in file order,
//      after the window's events of that frame: an Event command is handled
//      as if the window had produced it; a Dump command dumps that frame
//      to its file, as P does to --dump's, with the same stdout line. The
//      run ends after the last command's frame is drawn (after the first
//      drawn frame when the script has no commands), or earlier on Escape
//      or --seconds. A script needs --asset, as --dump does.
//   4. A scripted run is a pure function of the script: with --script the
//      seconds a held key is applied for each frame are kScriptFrameSeconds,
//      not the clock (the window's own events still arrive and are handled
//      first). Every dump line, scripted or not, carries the camera after
//      that frame's commands and held keys:
//      `dump=<file> size=<w>x<h> yaw=<y> pitch=<p> distance=<d>
//      offset=<x>,<y>,<z>` on one line, each of the six numbers the
//      shortest decimal of up to nine significant digits that rounds to
//      the float (a zero is `0`), so a test reads the camera the frame was
//      drawn from.
//   5. The viewer's handling of the events, which a script relies on
//      (bw/rendering/scene/orbit.hpp for the camera functions): a move with
//      Left held turns the camera by the pixels since the press or the
//      last such move at kOrbitRadiansPerPixel; otherwise a move with Right
//      held pans it by those pixels at world_per_pixel of the viewport's
//      height in pixels (nothing when that height is 0); a scroll dollies
//      by its lines. Held keys act each drawn frame for dt of contract 4:
//      Right minus Left and Down minus Up held are a drag of
//      kOrbitKeyPixelsPerSecond * dt pixels that turns, D minus A and S
//      minus W held one that pans, Minus minus Equal held is a dolly of
//      kOrbitKeyLinesPerSecond * dt lines. R resets the camera to the
//      current asset's rest camera, LeftBracket and RightBracket step to
//      the previous and next --asset (wrapping) and reset, P dumps the
//      next frame drawn, Escape ends the run; Other does nothing. A key is
//      held from its pressed event to its released event or the window
//      losing focus.
//
// Core guidelines read: I.1 and Con.5 (the parse reads the text and nothing
// else), E.27 (named errors with the line and word), F.20 (commands by
// value), Enum.3.
namespace bw::character_viewer {

// The frame time a scripted run applies held keys for (contract 4).
inline constexpr double kScriptFrameSeconds = 1.0 / 60.0;

enum class ScriptCommandKind : std::uint8_t { Event, Dump };

struct ScriptCommand {
  ScriptCommandKind kind{ScriptCommandKind::Event};
  platform::WindowEvent event{};
  // Set for Dump.
  std::string file{};
};

enum class ScriptError : std::uint8_t {
  None,
  UnknownCommand,
  ArgumentCount,
  InvalidNumber,
  UnknownKey,
  UnknownButton,
  EmptyDumpFile,
};

struct ScriptParse {
  std::vector<ScriptCommand> commands{};
  ScriptError error{ScriptError::None};
  std::size_t line{};
  std::string_view word{};
  [[nodiscard]] bool ok() const noexcept { return error == ScriptError::None; }
};

[[nodiscard]] ScriptParse parse_script(std::string_view text);

} // namespace bw::character_viewer

#endif
