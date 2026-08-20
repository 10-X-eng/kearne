#include <kearne/testkit/property.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (condition)
    return;
  std::cerr << message << '\n';
  std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
  using namespace kearne::testkit;

  PropertyProfile replay;
  replay.replay = PropertyProfile::ReplayCase{249, 15355447900275867851ULL};
  std::uint64_t calls = 0;
  checkProperty("direct replay", replay,
                [&](Random &random, std::uint64_t index) {
                  Random expected{replay.replay->seed};
                  ++calls;
                  require(index == replay.replay->index &&
                              random.next() == expected.next(),
                          "replay did not preserve the reported case");
                });
  require(calls == 1, "replay executed more than one case");

  PropertyProfile sharded;
  sharded.seed = 17;
  sharded.iterations = 101;
  sharded.shardIndex = 2;
  sharded.shardCount = 4;
  std::uint64_t shardedCalls = 0;
  checkProperty("sharded sequence", sharded,
                [&](Random &, std::uint64_t index) {
                  ++shardedCalls;
                  require(index % sharded.shardCount == sharded.shardIndex,
                          "shard executed the wrong case");
                });
  require(shardedCalls == 25, "shard executed the wrong number of cases");

  try {
    checkProperty("failing replay", replay, [](Random &, std::uint64_t) {
      throw std::runtime_error("sentinel");
    });
    require(false, "failing replay did not fail");
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    require(message.find("iteration 249") != std::string::npos &&
                message.find("seed 15355447900275867851") !=
                    std::string::npos &&
                message.ends_with("sentinel"),
            "replay failure omitted reproduction evidence");
  }
}
