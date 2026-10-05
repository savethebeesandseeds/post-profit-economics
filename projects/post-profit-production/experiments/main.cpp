// SPDX-License-Identifier: MIT
#include "experiment.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::string read(std::istream& stream) {
  std::string result;
  char buffer[4096];
  while (stream.read(buffer, sizeof(buffer)) || stream.gcount()) {
    if (result.size() + static_cast<std::size_t>(stream.gcount()) > 2 * 1024 * 1024)
      throw std::invalid_argument("plan exceeds 2 MiB");
    result.append(buffer, static_cast<std::size_t>(stream.gcount()));
  }
  if (stream.bad()) throw std::runtime_error("could not read plan");
  return result;
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
      std::cout << "Usage: production_experiment --plan PATH [--format json|markdown]\n"
                   "Run a bounded production.experiment.config.v1 plan; output goes to stdout.\n"
                   "Exit codes: 0 successful checks, 2 verification failed, 1 input/argument error.\n";
      return 0;
    }
    std::string path, format = "json";
    bool saw_plan = false, saw_format = false;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--plan" && !saw_plan && i + 1 < argc) {
        path = argv[++i]; saw_plan = true;
      } else if (argument == "--format" && !saw_format && i + 1 < argc) {
        format = argv[++i]; saw_format = true;
      } else throw std::invalid_argument("invalid or duplicate arguments; use --help");
    }
    if (!saw_plan || path.empty()) throw std::invalid_argument("supply --plan PATH");
    if (format != "json" && format != "markdown") throw std::invalid_argument("format must be json or markdown");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("could not open plan");
    const auto plan = ph::production::parse_json(read(file));
    const auto report = ph::production::research::run_experiment(plan);
    std::cout << (format == "json" ? report.dump(2) + "\n" : ph::production::research::render_markdown(report));
    return report.at("status") == "ok" ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
