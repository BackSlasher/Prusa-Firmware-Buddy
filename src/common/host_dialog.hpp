/// @file
/// @brief Mirror the printer's top-most dialog to the USB serial host using Marlin's
/// host prompt protocol (`//action:prompt_begin` ... `//action:prompt_show`, answered
/// with `M876 S<button index>`), so a host such as OctoPrint can show and answer the
/// same dialog the LCD shows (tool pickup failed, nozzle dirty, quick pause, ...).
///
/// All serial output happens from the Marlin server thread (polled from idle() via
/// host_keepalive()); USBSerial::write is not thread safe, so nothing here may be
/// called from GUI or feature threads. `M876` is handled at serial read time
/// (GCodeQueue::get_serial_commands) because the gcode queue does not advance
/// while a dialog blocks the current command.
#pragma once

#include <cstdint>

namespace host_dialog {

/// Poll the FSM state (rate limited); emits prompt/notification lines when the
/// top-most dialog appears, changes phase or disappears. Marlin server thread only.
void poll();

/// Emit the current dialog again (host reconnected: M115, or M876 P1).
void reemit();

/// Answer the current dialog with the n-th button as it was emitted.
/// @return nullptr on success, otherwise a short reason.
const char *answer(uint8_t button_index);

/// Handle a raw serial line if it is an M876; accepts "N<n> M876 ... *<cs>" too.
/// @return true when the line was an M876 and has been handled here.
bool handle_serial_line(const char *line);

} // namespace host_dialog
