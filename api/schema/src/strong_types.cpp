#include <kearne/api/strong_types.hpp>

#include <exception>

namespace kearne::api {

Result<Origin> readOrigin(v1::Origin value) {
  switch (value) {
  case v1::ORIGIN_HUMAN:
    return Origin::Human;
  case v1::ORIGIN_PYTHON:
    return Origin::Python;
  case v1::ORIGIN_AI:
    return Origin::AI;
  case v1::ORIGIN_PLUGIN:
    return Origin::Plugin;
  case v1::ORIGIN_IMPORT:
    return Origin::Import;
  case v1::ORIGIN_REPLAY:
    return Origin::Replay;
  case v1::ORIGIN_SYSTEM:
    return Origin::System;
  default:
    return std::unexpected(
        diagnostic("api.origin.unspecified", "wire origin is unspecified"));
  }
}

v1::Origin writeOrigin(Origin value) {
  switch (value) {
  case Origin::Human:
    return v1::ORIGIN_HUMAN;
  case Origin::Python:
    return v1::ORIGIN_PYTHON;
  case Origin::AI:
    return v1::ORIGIN_AI;
  case Origin::Plugin:
    return v1::ORIGIN_PLUGIN;
  case Origin::Import:
    return v1::ORIGIN_IMPORT;
  case Origin::Replay:
    return v1::ORIGIN_REPLAY;
  case Origin::System:
    return v1::ORIGIN_SYSTEM;
  }
  std::terminate();
}

void writeDiagnostic(const Diagnostic &value, v1::Diagnostic *wire) {
  wire->set_code(value.code);
  switch (value.severity) {
  case Severity::Information:
    wire->set_severity(v1::SEVERITY_INFORMATION);
    break;
  case Severity::Warning:
    wire->set_severity(v1::SEVERITY_WARNING);
    break;
  case Severity::Error:
    wire->set_severity(v1::SEVERITY_ERROR);
    break;
  case Severity::Fatal:
    wire->set_severity(v1::SEVERITY_FATAL);
    break;
  }
  for (const std::string &parameter : value.parameters)
    wire->add_parameters(parameter);
}

} // namespace kearne::api
