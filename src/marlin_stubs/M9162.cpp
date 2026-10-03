#include "PrusaGcodeSuite.hpp"

#include <gcode/gcode_parser.hpp>
#include <Marlin/src/core/serial.h>
#include <option/xbuddy_extension_variant.h>

#if XBUDDY_EXTENSION_VARIANT_IS_STANDARD()

    #include <feature/xbuddy_extension/xbuddy_extension.hpp>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M9162: Get/set whether the bed LED strip goes dark while the loadcell is probing
 *
 * The PWM of the bed LED strip disturbs the loadcell reading, so the strip is held dark while
 * the loadcell is probing. This switches that off and on at runtime, to compare the two.
 * Enabled after every boot, the setting is not stored.
 *
 *#### Usage
 *
 *    M9162 [ S ]
 *
 *#### Parameters
 *
 * - `S` - `0` leave the strip alone, `1` dark while probing. Without it, only the state is reported.
 *
 * Always reports one line: `M9162 bed_leds_dark_while_probing:<0|1>`
 */
void PrusaGcodeSuite::M9162() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    if (const auto enable = p.option<bool>('S')) {
        buddy::xbuddy_extension().set_bed_leds_dark_for_probing_enabled(*enable);
    }

    SERIAL_ECHOLNPAIR("M9162 bed_leds_dark_while_probing:", buddy::xbuddy_extension().bed_leds_dark_for_probing_enabled() ? 1 : 0);
}

/** @}*/

#endif
