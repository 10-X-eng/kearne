#include <kearne/document/content_store.hpp>
#include <kearne/document/content_store_access.hpp>

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace kearne::document {

Result<ContentDigest> contentDigest(std::span<const std::uint8_t> bytes) {
  return hashCanonical<ContentDigest>(contentBlobDigestContext, bytes);
}

struct InMemoryContentStore::Impl {
  explicit Impl(ContentStoreLimits configuredLimits)
      : limits(configuredLimits) {}

  ContentStoreLimits limits;
  std::size_t byteSize = 0;
  mutable std::shared_mutex mutex;
  std::unordered_map<ContentDigest, std::shared_ptr<const Bytes>,
                     TypedDigestHash<ContentDigestTag>>
      content;
};

InMemoryContentStore::InMemoryContentStore(ContentStoreLimits limits)
    : impl_(std::make_unique<Impl>(limits)) {}
InMemoryContentStore::~InMemoryContentStore() = default;

Result<void> InMemoryContentStore::put(ContentDigest digest, Bytes bytes) {
  if (bytes.size() > impl_->limits.maxBlobBytes)
    return std::unexpected(diagnostic("document.content.blob-too-large",
                                      "content exceeds the per-blob limit"));
  auto actual = contentDigest(bytes);
  if (!actual)
    return std::unexpected(std::move(actual.error()));
  if (*actual != digest)
    return std::unexpected(diagnostic("document.content.digest-mismatch",
                                      "content does not match its digest"));

  std::unique_lock lock{impl_->mutex};
  const auto found = impl_->content.find(digest);
  if (found != impl_->content.end()) {
    if (*found->second != bytes)
      return std::unexpected(
          diagnostic("document.content.collision",
                     "content digest collision or corruption was detected",
                     Severity::Fatal));
    return {};
  }
  if (impl_->byteSize > impl_->limits.maxTotalBytes ||
      bytes.size() > impl_->limits.maxTotalBytes - impl_->byteSize)
    return std::unexpected(diagnostic("document.content.capacity-exceeded",
                                      "content store capacity is exhausted"));
  auto stored = std::make_shared<const Bytes>(std::move(bytes));
  const bool inserted =
      impl_->content.emplace(std::move(digest), stored).second;
  if (!inserted)
    return std::unexpected(diagnostic(
        "document.content.concurrent-invariant",
        "content publication violated the store lock", Severity::Fatal));
  impl_->byteSize += stored->size();
  return {};
}

Result<std::shared_ptr<const Bytes>>
InMemoryContentStore::get(const ContentDigest &digest) const {
  std::shared_lock lock{impl_->mutex};
  const auto found = impl_->content.find(digest);
  if (found == impl_->content.end())
    return std::unexpected(
        diagnostic("document.content.not-found", "content is not available"));
  return found->second;
}

std::size_t InMemoryContentStore::size() const {
  std::shared_lock lock{impl_->mutex};
  return impl_->content.size();
}

std::size_t InMemoryContentStore::byteSize() const {
  std::shared_lock lock{impl_->mutex};
  return impl_->byteSize;
}

ContentStoreLimits InMemoryContentStore::limits() const {
  return impl_->limits;
}

void internal::ContentStoreFaultAccess::injectUnverified(
    InMemoryContentStore &store, ContentDigest digest, Bytes bytes) {
  std::unique_lock lock{store.impl_->mutex};
  const auto prior = store.impl_->content.find(digest);
  const std::size_t priorSize =
      prior == store.impl_->content.end() ? 0 : prior->second->size();
  auto stored = std::make_shared<const Bytes>(std::move(bytes));
  store.impl_->content.insert_or_assign(std::move(digest), stored);
  store.impl_->byteSize = store.impl_->byteSize - priorSize + stored->size();
}

} // namespace kearne::document
