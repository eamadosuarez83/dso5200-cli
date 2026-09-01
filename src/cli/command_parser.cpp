// SPDX-License-Identifier: GPL-2.0+

#include "command_parser.h"

#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

namespace Cli {

namespace {

std::vector<std::string> tokenize(const std::string &line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

/// Parses values like "1ms", "500us", "2s", "100ns" into seconds.
std::optional<double> parseDuration(const std::string &text) {
    size_t splitAt = text.size();
    while (splitAt > 0 && !std::isdigit((unsigned char)text[splitAt - 1]) && text[splitAt - 1] != '.') --splitAt;
    if (splitAt == 0 || splitAt == text.size()) return std::nullopt;

    std::string numberPart = text.substr(0, splitAt);
    std::string unitPart = text.substr(splitAt);

    double value;
    try {
        size_t consumed;
        value = std::stod(numberPart, &consumed);
        if (consumed != numberPart.size()) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }

    double multiplier;
    if (unitPart == "s")
        multiplier = 1.0;
    else if (unitPart == "ms")
        multiplier = 1e-3;
    else if (unitPart == "us")
        multiplier = 1e-6;
    else if (unitPart == "ns")
        multiplier = 1e-9;
    else
        return std::nullopt;

    return value * multiplier;
}

std::optional<double> parseNumber(const std::string &text) {
    try {
        size_t consumed;
        double value = std::stod(text, &consumed);
        if (consumed != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> parseChannelIndex(const std::string &text) {
    if (text == "1") return 0;
    if (text == "2") return 1;
    return std::nullopt;
}

} // namespace

void printHelp() {
    std::cout << "Comandos disponibles:\n"
              << "  timebase <valor><s|ms|us|ns>   Ajusta la duracion total de pantalla (ej: timebase 1ms)\n"
              << "  volts <1|2> <V/div>            Ajusta volts/division del canal (ej: volts 1 0.5)\n"
              << "  channel <1|2|both|none>        Activa/desactiva canales\n"
              << "  slope <rising|falling>         Pendiente de disparo\n"
              << "  start                          Inicia el muestreo\n"
              << "  stop                           Detiene el muestreo\n"
              << "  status                         Muestra el estado actual\n"
              << "  help                           Muestra esta ayuda\n"
              << "  quit                           Sale del programa\n";
}

void handleLine(const std::string &line, IDsoSource &source, bool &quit) {
    std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty()) return;

    const std::string &cmd = tokens[0];

    if (cmd == "help" || cmd == "?") {
        printHelp();
    } else if (cmd == "quit" || cmd == "exit") {
        quit = true;
    } else if (cmd == "start") {
        source.start();
        std::cout << "Muestreo iniciado.\n";
    } else if (cmd == "stop") {
        source.stop();
        std::cout << "Muestreo detenido.\n";
    } else if (cmd == "status") {
        std::cout << source.statusText() << "\n";
    } else if (cmd == "timebase") {
        if (tokens.size() != 2) {
            std::cout << "Uso: timebase <valor><s|ms|us|ns>\n";
            return;
        }
        auto seconds = parseDuration(tokens[1]);
        if (!seconds || !source.setTimebase(*seconds)) {
            std::cout << "Valor de timebase invalido: " << tokens[1] << "\n";
            return;
        }
        std::cout << "Timebase ajustado a " << tokens[1] << " de pantalla.\n";
    } else if (cmd == "volts") {
        if (tokens.size() != 3) {
            std::cout << "Uso: volts <1|2> <V/div>\n";
            return;
        }
        auto channel = parseChannelIndex(tokens[1]);
        auto volts = parseNumber(tokens[2]);
        if (!channel || !volts || !source.setVoltsPerDiv(*channel, *volts)) {
            std::cout << "Parametros invalidos para volts.\n";
            return;
        }
        std::cout << "Canal " << tokens[1] << " ajustado a " << tokens[2] << " V/div.\n";
    } else if (cmd == "channel") {
        if (tokens.size() != 2) {
            std::cout << "Uso: channel <1|2|both|none>\n";
            return;
        }
        const std::string &arg = tokens[1];
        bool ok = true;
        if (arg == "1") {
            ok = source.setChannelEnabled(0, true) && source.setChannelEnabled(1, false);
        } else if (arg == "2") {
            ok = source.setChannelEnabled(0, false) && source.setChannelEnabled(1, true);
        } else if (arg == "both") {
            ok = source.setChannelEnabled(0, true) && source.setChannelEnabled(1, true);
        } else if (arg == "none") {
            ok = source.setChannelEnabled(0, false) && source.setChannelEnabled(1, false);
        } else {
            ok = false;
        }
        std::cout << (ok ? "Canales actualizados.\n" : "Argumento invalido para channel.\n");
    } else if (cmd == "slope") {
        if (tokens.size() != 2 || (tokens[1] != "rising" && tokens[1] != "falling")) {
            std::cout << "Uso: slope <rising|falling>\n";
            return;
        }
        source.setTriggerSlope(tokens[1] == "rising" ? TriggerSlope::Rising : TriggerSlope::Falling);
        std::cout << "Pendiente de disparo: " << tokens[1] << "\n";
    } else {
        std::cout << "Comando desconocido: " << cmd << " (escribe 'help')\n";
    }
}

} // namespace Cli
