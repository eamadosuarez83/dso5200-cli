// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <string>

#include "device/idsosource.h"

namespace Cli {

/// Parses and applies a single interactive command line (see README.md for the
/// command reference). Prints feedback/errors to stdout. Sets `quit` to true
/// when the user asked to exit.
void handleLine(const std::string &line, IDsoSource &source, bool &quit);

void printHelp();

} // namespace Cli
