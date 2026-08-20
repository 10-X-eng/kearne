#include <kearne/api/wire_validation.hpp>

#include <absl/base/log_severity.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <google/protobuf/message.h>

#include <cstddef>
#include <cstdint>
#include <memory>

void configureWireFuzzLogging() {
  absl::InitializeLog();
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfinity);
}

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  configureWireFuzzLogging();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  if (size == 0 || size > 131072)
    return 0;
  const auto descriptors = kearne::api::registeredWireTypes();
  const google::protobuf::Descriptor *descriptor =
      descriptors[data[0] % descriptors.size()];
  const google::protobuf::Message *prototype =
      google::protobuf::MessageFactory::generated_factory()->GetPrototype(
          descriptor);
  if (!prototype)
    return 0;
  auto message = std::unique_ptr<google::protobuf::Message>(prototype->New());
  if (message->ParseFromArray(data + 1, static_cast<int>(size - 1)))
    static_cast<void>(kearne::api::validateWire(*message));
  return 0;
}
