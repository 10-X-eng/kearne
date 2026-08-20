#pragma once

#include <kearne/document/content_store.hpp>

namespace kearne::document::internal {

class ContentStoreFaultAccess final {
public:
  static void injectUnverified(InMemoryContentStore &store,
                               ContentDigest digest, Bytes bytes);
};

} // namespace kearne::document::internal
