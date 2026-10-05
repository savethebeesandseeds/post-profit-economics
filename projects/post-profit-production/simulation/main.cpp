// SPDX-License-Identifier: MIT
#include "simulation.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
std::string read(std::istream& stream) {
  std::string result;
  char buffer[4096];
  while (stream.read(buffer, sizeof(buffer)) || stream.gcount()) {
    if (result.size() + static_cast<std::size_t>(stream.gcount()) > 2 * 1024 * 1024)
      throw std::invalid_argument("input exceeds 2 MiB");
    result.append(buffer, static_cast<std::size_t>(stream.gcount()));
  }
  if (stream.bad()) throw std::runtime_error("could not read input");
  return result;
}
}
int main(int argc, char** argv) {
  try {
    using namespace ph::production;
    std::string output;
    if (argc == 2 && std::string(argv[1]) == "--example-config") {
      std::cout << default_config().dump(2) << '\n'; return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--help") {
      std::cout << "Usage: production_simulation [--config PATH | --example-config | --help]\n"
                   "Without arguments, read one JSON defaults/simulate command from stdin.\n"
                   "Results go to stdout; no records are written automatically.\n";
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--config") {
      std::ifstream file(argv[2], std::ios::binary);
      if (!file) throw std::runtime_error("could not open configuration file");
      const auto config = parse_json(read(file));
      output = run_json(Json({{"op", "simulate"}, {"config", config}}).dump());
    } else {
      if (argc != 1) throw std::invalid_argument("invalid arguments; use --help");
      output = run_json(read(std::cin));
    }
    std::cout << output << '\n';
    return Json::parse(output).at("status") == "ok" ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
