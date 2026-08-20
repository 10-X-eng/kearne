#pragma once

#include <kearne/adapters/framed_worker_process.hpp>
#include <kearne/api/v1/worker.pb.h>
#include <kearne/sketch_workflow/workflow.hpp>

#include <cstdint>
#include <span>
#include <stop_token>
#include <string_view>

namespace kearne::adapters {

using SketchSourceTransformOutput = sketch_workflow::SourceRevision;

class SketchSourceWorker final : public sketch_workflow::SourceEditor {
public:
  SketchSourceWorker(FramedWorkerProcessConfig process,
                     WorkerInstanceId workerInstance);

  [[nodiscard]] Result<SketchSourceTransformOutput>
  transform(JobId job, const api::v1::SketchSourceTransformJob &request,
            std::stop_token cancellation = {});
  [[nodiscard]] Result<SketchSourceTransformOutput>
  create(JobId job, std::string_view functionName,
         std::stop_token cancellation = {}) override;
  [[nodiscard]] Result<SketchSourceTransformOutput>
  apply(JobId job, std::span<const std::uint8_t> source,
        std::string_view functionName, const sketch::AppliedEdits &edits,
        std::stop_token cancellation = {}) override;
  void stop();
  [[nodiscard]] std::uint64_t processGeneration() const {
    return process_.processGeneration();
  }

private:
  FramedWorkerProcess process_;
  WorkerInstanceId workerInstance_;
};

} // namespace kearne::adapters
