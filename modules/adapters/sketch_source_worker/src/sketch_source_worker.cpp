#include <kearne/adapters/sketch_source_worker.hpp>

#include <kearne/adapters/sketch_wire.hpp>
#include <kearne/api/strong_types.hpp>
#include <kearne/api/wire_validation.hpp>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/unknown_field_set.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace kearne::adapters {
namespace {

namespace protobuf = google::protobuf;
namespace wire = api::v1;

constexpr std::size_t maximumWorkerFrameBytes = 33'554'432U;
constexpr int maximumWorkerWireDepth = 32;

bool containsUnknownFields(const protobuf::Message &message) {
  const protobuf::Reflection &reflection = *message.GetReflection();
  if (reflection.GetUnknownFields(message).field_count() != 0)
    return true;
  const protobuf::Descriptor &descriptor = *message.GetDescriptor();
  for (int fieldIndex = 0; fieldIndex < descriptor.field_count();
       ++fieldIndex) {
    const protobuf::FieldDescriptor &field = *descriptor.field(fieldIndex);
    if (field.cpp_type() != protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
      continue;
    const int count = field.is_repeated()
                          ? reflection.FieldSize(message, &field)
                          : (reflection.HasField(message, &field) ? 1 : 0);
    for (int index = 0; index < count; ++index) {
      const protobuf::Message &child =
          field.is_repeated()
              ? reflection.GetRepeatedMessage(message, &field, index)
              : reflection.GetMessage(message, &field);
      if (containsUnknownFields(child))
        return true;
    }
  }
  return false;
}

Result<wire::WorkerResultEnvelope> parseResult(const QByteArray &bytes) {
  if (bytes.isEmpty() ||
      static_cast<std::size_t>(bytes.size()) > maximumWorkerFrameBytes)
    return std::unexpected(diagnostic("worker.source.result-limit",
                                      "source worker result is too large"));
  protobuf::io::CodedInputStream input(
      reinterpret_cast<const std::uint8_t *>(bytes.constData()),
      static_cast<int>(bytes.size()));
  input.SetTotalBytesLimit(static_cast<int>(maximumWorkerFrameBytes));
  input.SetRecursionLimit(maximumWorkerWireDepth);
  wire::WorkerResultEnvelope result;
  if (!result.ParseFromCodedStream(&input) || !input.ConsumedEntireMessage())
    return std::unexpected(diagnostic("worker.source.result-parse",
                                      "source worker result is malformed"));
  if (containsUnknownFields(result))
    return std::unexpected(
        diagnostic("worker.source.result-unknown-field",
                   "source worker result contains an unsupported field"));
  if (auto valid = api::validateWire(result); !valid)
    return std::unexpected(std::move(valid.error()));
  return result;
}

Diagnostic readFailure(const wire::SketchSourceTransformFailure &failure) {
  if (failure.diagnostics().empty())
    return diagnostic("worker.source.failed",
                      "source worker returned no diagnostic");
  const wire::Diagnostic &value = failure.diagnostics(0);
  Severity severity = Severity::Error;
  switch (value.severity()) {
  case wire::SEVERITY_INFORMATION:
    severity = Severity::Information;
    break;
  case wire::SEVERITY_WARNING:
    severity = Severity::Warning;
    break;
  case wire::SEVERITY_FATAL:
    severity = Severity::Fatal;
    break;
  case wire::SEVERITY_ERROR:
  case wire::SEVERITY_UNSPECIFIED:
    break;
  default:
    break;
  }
  Diagnostic result =
      diagnostic(std::string{value.code().data(), value.code().size()},
                 "source worker rejected the transform", severity);
  result.parameters.assign(value.parameters().begin(),
                           value.parameters().end());
  return result;
}

wire::SketchSourceEditAction action(sketch::SourceEditAction value) {
  switch (value) {
  case sketch::SourceEditAction::Append:
    return wire::SKETCH_SOURCE_EDIT_ACTION_APPEND;
  case sketch::SourceEditAction::Replace:
    return wire::SKETCH_SOURCE_EDIT_ACTION_REPLACE;
  case sketch::SourceEditAction::Delete:
    return wire::SKETCH_SOURCE_EDIT_ACTION_DELETE;
  }
  return wire::SKETCH_SOURCE_EDIT_ACTION_UNSPECIFIED;
}

wire::SketchSourceSection section(sketch::SourceSection value) {
  switch (value) {
  case sketch::SourceSection::Objects:
    return wire::SKETCH_SOURCE_SECTION_OBJECTS;
  case sketch::SourceSection::Entities:
    return wire::SKETCH_SOURCE_SECTION_ENTITIES;
  case sketch::SourceSection::Constraints:
    return wire::SKETCH_SOURCE_SECTION_CONSTRAINTS;
  }
  return wire::SKETCH_SOURCE_SECTION_UNSPECIFIED;
}

} // namespace

SketchSourceWorker::SketchSourceWorker(FramedWorkerProcessConfig process,
                                       WorkerInstanceId workerInstance)
    : process_(std::move(process)), workerInstance_(std::move(workerInstance)) {
}

Result<SketchSourceTransformOutput>
SketchSourceWorker::transform(JobId job,
                              const wire::SketchSourceTransformJob &request,
                              std::stop_token cancellation) {
  wire::WorkerJobEnvelope envelope;
  api::writeId(workerInstance_, envelope.mutable_worker_instance_id());
  api::writeId(job, envelope.mutable_job_id());
  *envelope.mutable_sketch_source_transform() = request;
  if (containsUnknownFields(envelope))
    return std::unexpected(
        diagnostic("worker.source.request-unknown-field",
                   "source worker request contains an unsupported field"));
  if (auto valid = api::validateWire(envelope); !valid)
    return std::unexpected(std::move(valid.error()));
  std::string encoded;
  if (!envelope.SerializeToString(&encoded) || encoded.empty() ||
      encoded.size() > maximumWorkerFrameBytes)
    return std::unexpected(diagnostic("worker.source.request-serialize",
                                      "source worker request is invalid"));
  auto response = process_.exchange(
      QByteArray{encoded.data(), static_cast<qsizetype>(encoded.size())},
      cancellation);
  if (!response)
    return std::unexpected(std::move(response.error()));
  auto result = parseResult(*response);
  if (!result)
    return std::unexpected(std::move(result.error()));
  auto worker = api::readId<WorkerInstanceId>(result->worker_instance_id());
  auto returnedJob = api::readId<JobId>(result->job_id());
  if (!worker || !returnedJob || *worker != workerInstance_ ||
      *returnedJob != job)
    return std::unexpected(
        diagnostic("worker.source.correlation",
                   "source worker result does not match the dispatched job",
                   Severity::Fatal));
  const wire::SketchSourceTransformResult &transform =
      result->sketch_source_transform();
  if (transform.outcome_case() == wire::SketchSourceTransformResult::kFailure)
    return std::unexpected(readFailure(transform.failure()));
  if (transform.outcome_case() != wire::SketchSourceTransformResult::kSuccess)
    return std::unexpected(diagnostic("worker.source.missing-result",
                                      "source worker returned no outcome"));
  const auto source = transform.success().source_utf8();
  document::Bytes bytes(source.begin(), source.end());
  auto returnedDigest =
      api::readDigest<ContentDigest>(transform.success().source_digest());
  auto actualDigest = document::contentDigest(bytes);
  if (!returnedDigest || !actualDigest || *returnedDigest != *actualDigest)
    return std::unexpected(
        diagnostic("worker.source.digest-mismatch",
                   "source worker result does not match its content digest",
                   Severity::Fatal));
  auto definition = readSketchDefinition(transform.success().definition());
  if (!definition || definition->sourceDigest != *actualDigest)
    return std::unexpected(
        definition
            ? diagnostic("worker.source.definition-digest",
                         "source worker definition does not match its source",
                         Severity::Fatal)
            : std::move(definition.error()));
  return SketchSourceTransformOutput{{std::move(bytes), *actualDigest},
                                     std::move(*definition)};
}

Result<sketch_workflow::SourceRevision>
SketchSourceWorker::create(JobId job, std::string_view functionName,
                           std::stop_token cancellation) {
  wire::SketchSourceTransformJob request;
  request.mutable_create()->set_function_name(functionName);
  auto transformed = transform(job, request, cancellation);
  if (!transformed)
    return std::unexpected(std::move(transformed.error()));
  if (!transformed->definition.objects.empty() ||
      !transformed->definition.entities.empty() ||
      !transformed->definition.constraints.empty())
    return std::unexpected(diagnostic("worker.source.create-definition",
                                      "source worker created a nonempty Sketch",
                                      Severity::Fatal));
  return std::move(transformed->source);
}

Result<sketch_workflow::SourceRevision>
SketchSourceWorker::apply(JobId job, std::span<const std::uint8_t> source,
                          std::string_view functionName,
                          const sketch::AppliedEdits &edits,
                          std::stop_token cancellation) {
  auto sourceDigest = document::contentDigest(source);
  if (!sourceDigest)
    return std::unexpected(std::move(sourceDigest.error()));
  if (*sourceDigest != edits.target.sourceDigest)
    return std::unexpected(
        diagnostic("worker.source.stale-input",
                   "Sketch edits were derived from another source"));

  wire::SketchSourceTransformJob request;
  wire::EditSketchSource *wireEdit = request.mutable_edit();
  wireEdit->set_source_utf8(std::string{
      reinterpret_cast<const char *>(source.data()), source.size()});
  wireEdit->set_function_name(functionName);
  api::writeDigest(*sourceDigest, wireEdit->mutable_expected_prior());
  if (auto written =
          writeSketchDefinition(edits.target, wireEdit->mutable_target());
      !written)
    return std::unexpected(std::move(written.error()));
  for (const sketch::SourceEditIntent &edit : edits.sourceEdits) {
    wire::SketchSourceEdit *wireOperation = wireEdit->add_edits();
    wireOperation->set_action(action(edit.action));
    wireOperation->set_section(section(edit.section));
    std::visit(
        [wireOperation](const auto &id) {
          api::writeId(id, wireOperation->mutable_target_id());
        },
        edit.target);
  }
  auto transformed = transform(job, request, cancellation);
  if (!transformed)
    return std::unexpected(std::move(transformed.error()));
  sketch::Definition expected = edits.target;
  expected.sourceDigest = transformed->source.digest;
  if (transformed->definition != expected)
    return std::unexpected(
        diagnostic("worker.source.definition-mismatch",
                   "source worker definition does not match the requested edit",
                   Severity::Fatal));
  return std::move(transformed->source);
}

Result<SketchSourceTransformOutput>
SketchSourceWorker::replace(JobId job, std::span<const std::uint8_t> source,
                            std::string_view functionName,
                            const ContentDigest &expectedPrior,
                            std::stop_token cancellation) {
  auto actual = document::contentDigest(source);
  if (!actual)
    return std::unexpected(std::move(actual.error()));
  if (*actual == expectedPrior)
    return std::unexpected(diagnostic("worker.source.unchanged",
                                      "replacement source is unchanged"));
  wire::SketchSourceTransformJob request;
  wire::ReplaceSketchSource *replacement = request.mutable_replace();
  replacement->set_source_utf8(std::string{
      reinterpret_cast<const char *>(source.data()), source.size()});
  replacement->set_function_name(functionName);
  api::writeDigest(expectedPrior, replacement->mutable_expected_prior());
  return transform(job, request, cancellation);
}

void SketchSourceWorker::stop() { process_.stop(); }

} // namespace kearne::adapters
