/**
 * DisplayController stub for native unit testing.
 *
 * Only the symbols referenced by scenarios.cpp (the Display global and
 * showOverride) need a definition; the rendering path is hardware-only.
 */
#include "../../src/display.h"

DisplayController Display;

void DisplayController::showOverride(const String&, const String&, const String&,
                                     const String&, uint32_t) {}
