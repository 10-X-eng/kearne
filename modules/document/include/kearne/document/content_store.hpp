#pragma once

#include <kearne/document/canonical.hpp>

#include <cstddef>
#include <memory>
#include <span>

namespace kearne::document {

namespace internal {
class ContentStoreFaultAccess;
}

inline constexpr std::string_view contentBlobDigestContext =
    "kearne.content.blob.v1";

[[nodiscard]] Result<ContentDigest>
contentDigest(std::span<const std::uint8_t> bytes);

struct ContentStoreLimits {
  std::size_t maxBlobBytes;
  std::size_t maxTotalBytes;
  bool operator==(const ContentStoreLimits &) const = default;
};

class ContentStore {
public:
  virtual ~ContentStore() = default;
  [[nodiscard]] virtual Result<void> put(ContentDigest digest, Bytes bytes) = 0;
  [[nodiscard]] virtual Result<std::shared_ptr<const Bytes>>
  get(const ContentDigest &digest) const = 0;
  [[nodiscard]] virtual ContentStoreLimits limits() const = 0;
};

class InMemoryContentStore final : public ContentStore {
public:
  explicit InMemoryContentStore(ContentStoreLimits limits);
  ~InMemoryContentStore() override;
  InMemoryContentStore(const InMemoryContentStore &) = delete;
  InMemoryContentStore &operator=(const InMemoryContentStore &) = delete;

  [[nodiscard]] Result<void> put(ContentDigest digest, Bytes bytes) override;
  [[nodiscard]] Result<std::shared_ptr<const Bytes>>
  get(const ContentDigest &digest) const override;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t byteSize() const;
  [[nodiscard]] ContentStoreLimits limits() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend class internal::ContentStoreFaultAccess;
};

} // namespace kearne::document
