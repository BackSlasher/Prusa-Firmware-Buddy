/// @file
#include "host_dialog.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>

#include <state/printer_state.hpp>
#include <marlin_vars.hpp>
#include <marlin_server.hpp>
#include <fsm_states.hpp>
#include <client_response.hpp>
#include <client_response_texts.hpp>
#include <encoded_fsm_response.hpp>
#include <error_codes.hpp>
#include <error_list.hpp>
#include <timing.h>
#include <gcode/gcode.h>

namespace host_dialog {

namespace {

    /// Generation id of the dialog last emitted to the host (0 = none)
    fsm::StateId last_emitted_gen = 0;
    /// A prompt_begin without a prompt_end is open on the host side
    bool prompt_open = false;
    uint32_t last_poll_ms = 0;
    constexpr uint32_t poll_interval_ms = 100;

    /// Print a C string on one line: newlines become spaces (prompt text is single-line)
    void print_oneline(const char *s) {
        if (!s) {
            return;
        }
        for (; *s; ++s) {
            SERIAL_CHAR((*s == '\n' || *s == '\r') ? ' ' : *s);
        }
    }

    const ErrDesc *find_error_or_null(ErrCode code) {
        const auto it = std::ranges::find_if(error_list, [code](const auto &e) { return e.err_code == code; });
        return it == std::end(error_list) ? nullptr : &*it;
    }

    void emit_end() {
        if (prompt_open) {
            SERIAL_ECHOLNPGM("//action:prompt_end");
            prompt_open = false;
        }
    }

    void emit_dialog(const printer_state::StateWithDialog &s) {
        const char *title = "Printer dialog";
        const char *text = "";
        if (s.has_code()) {
            if (const ErrDesc *err = find_error_or_null(*s.dialog->code)) {
                title = err->err_title;
                text = err->err_text;
            }
        }

        const Response *buttons = s.buttons();
        const bool has_buttons = buttons && buttons[0] != Response::_none;

        emit_end();
        if (!has_buttons) {
            // Progress-like dialogs (parking, homing, ...) - just tell the host what is going on
            SERIAL_ECHOPGM("//action:notification ");
            print_oneline(title);
            SERIAL_EOL();
            return;
        }

        SERIAL_ECHOPGM("//action:prompt_begin ");
        print_oneline(title);
        if (text && *text) {
            SERIAL_ECHOPGM(": ");
            print_oneline(text);
        }
        SERIAL_EOL();
        for (size_t i = 0; i < MAX_RESPONSES && buttons[i] != Response::_none; ++i) {
            SERIAL_ECHOPGM("//action:prompt_button ");
            print_oneline(get_response_text(buttons[i]));
            SERIAL_EOL();
        }
        SERIAL_ECHOLNPGM("//action:prompt_show");
        prompt_open = true;
    }

    void emit_current(bool force) {
        const auto s = printer_state::get_state_with_dialog(false);
        const fsm::StateId gen = s.dialog.has_value() ? s.dialog->dialog_id : 0;
        if (!force && gen == last_emitted_gen) {
            return;
        }
        last_emitted_gen = gen;
        if (!s.dialog.has_value()) {
            emit_end();
            return;
        }
        emit_dialog(s);
    }

} // namespace

void poll() {
    const uint32_t now = ticks_ms();
    if (now - last_poll_ms < poll_interval_ms) {
        return;
    }
    last_poll_ms = now;
    emit_current(false);
}

void reemit() {
    prompt_open = false; // the host may have forgotten the prompt (reconnect); start clean
    emit_current(true);
}

const char *answer(uint8_t button_index) {
    std::optional<fsm::States::Top> top;
    fsm::StateId gen = 0;
    marlin_vars().peek_fsm_states([&](const auto &states) {
        gen = states.get_state_id();
        top = states.get_top();
    });
    if (!top.has_value()) {
        return "no dialog";
    }
    if (gen != last_emitted_gen) {
        return "dialog changed";
    }
    const FSMAndPhase fsm_and_phase(top->fsm_type, top->data.GetPhase());
    const Response response = ClientResponses::get_available_response(fsm_and_phase, button_index);
    if (response == Response::_none) {
        return "invalid button";
    }
    marlin_server::set_response(EncodedFSMResponse {
        .response = FSMResponseVariant::make(response),
        .fsm_and_phase = fsm_and_phase,
    });
    return nullptr;
}

bool handle_serial_line(const char *line) {
    if (!line) {
        return false;
    }
    const char *p = line;
    while (*p == ' ') {
        ++p;
    }
    if (*p == 'N') { // optional line number
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++p;
        }
        while (*p == ' ') {
            ++p;
        }
    }
    if (strncmp(p, "M876", 4) != 0 || (p[4] != ' ' && p[4] != '\0' && p[4] != '*')) {
        return false;
    }
    p += 4;

    std::optional<uint8_t> button;
    bool reemit_requested = false;
    for (; *p && *p != '*' && *p != ';'; ++p) {
        if (*p == 'S') {
            button = static_cast<uint8_t>(strtoul(p + 1, nullptr, 10));
        } else if (*p == 'P' && p[1] == '1') {
            reemit_requested = true;
        }
    }

    SERIAL_ECHOPGM("echo:M876 ");
    if (reemit_requested) {
        SERIAL_ECHOLNPGM("reemit");
        reemit();
    } else if (button.has_value()) {
        if (const char *err = answer(*button)) {
            SERIAL_ECHOPGM("rejected: ");
            SERIAL_ECHOLN(err);
        } else {
            SERIAL_ECHOPGM("answered ");
            SERIAL_ECHOLN(int(*button));
        }
    } else {
        SERIAL_ECHOLNPGM("ignored (use S<button> or P1)");
    }
    return true;
}

} // namespace host_dialog
