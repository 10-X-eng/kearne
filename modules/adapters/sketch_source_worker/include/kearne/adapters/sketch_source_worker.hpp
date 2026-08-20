#pragma once

#include <kearne/adapters/framed_worker_process.hpp>
#include <kearne/api/v1/worker.pb.h>
#include <kearne/sketch_workflow/workflow.hpp>

#include <cstdint>
#include <span>
#include <stop_token>
#include <string_view>

namespace kearne::adapters {

struct SketchSourceTransformOutput {
  sketch_workflow::SourceRevision source;
  sketch::Definition definition;
};

class SketchSourceWorker final : public sketch_workflow::SourceEditor {
public:
  SketchSourceWorker(FramedWorkerProcessConfig process,
                     WorkerInstanceId workerInstance);

  [[nodiscard]] Result<SketchSourceTransformOutput>
  transform(JobId job, const api::v1::SketchSourceTransformJob &request,
            std::stop_token cancellation = {});
  [[nodiscard]] Result<sketch_workflow::SourceRevision>
  create(JobId job, std::string_view functionName,
         std::stop_token cancellation = {}) override;
  [[nodiscard]] Result<sketch_workflow::SourceRevision>
  apply(JobId job, std::span<const std::uint8_t> source,
        std::string_view functionName, const sketch::AppliedEdits &edits,
        std::stop_token cancellation = {}) override;
  [[nodiscard]] Result<SketchSourceTransformOutput>
  replace(JobId job, std::span<const std::uint8_t> source,
          std::string_view functionName, const ContentDigest &expectedPrior,
          std::stop_token cancellation = {});
  void stop();
  [[nodiscard]] std::uint64_t processGeneration() const {
    return process_.processGeneration();
  }

private:
  FramedWorkerProcess process_;
  WorkerInstanceId workerInstance_;
};

} // namespace kearne::adapters
