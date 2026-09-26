#include "ReaderReply.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
  const std::array<std::array<uint8_t, 5>, 3> replies{{
      {{0x22, 0x61, 0x41, 0x00, 0x00}},
      {{0x22, 0x61, 0x41, 0x05, 0x00}},
      {{0x33, 0x75, 0x02, 0x00, 0x00}},
  }};
  assert(!isReaderSuccessReply(nullptr, 5));
  assert(!isReaderSuccessReply(replies[0].data(), 0));
  for (const auto& first : replies) {
    assert(isReaderSuccessReply(first.data(), first.size()));
    for (const auto& second : replies) {
      std::vector<uint8_t> joined(first.begin(), first.end());
      joined.insert(joined.end(), second.begin(), second.end());
      assert(isReaderSuccessReply(joined.data(), joined.size()));
      for (const auto& third : replies) {
        auto triple = joined;
        triple.insert(triple.end(), third.begin(), third.end());
        assert(isReaderSuccessReply(triple.data(), triple.size()));
      }
    }
  }

  // 成功以外の応答、未知値、途中で切れた応答を握り潰さない。
  for (const auto& reply : replies) {
    auto failed = reply;
    failed[4] = 0x01;
    assert(!isReaderSuccessReply(failed.data(), failed.size()));
    for (size_t length = 1; length < reply.size(); ++length)
      assert(!isReaderSuccessReply(reply.data(), length));
  }
  const uint8_t unknown[] = {0x22, 0x61, 0x41, 0x03, 0x00};
  assert(!isReaderSuccessReply(unknown, sizeof(unknown)));
  const uint8_t binary[] = {0x00, 0xff, 0x02, 0x03, 0x04};
  assert(!isReaderSuccessReply(binary, sizeof(binary)));

  // 応答に似た印字可能コードや、応答と製品コードが混ざった結果は除外しない。
  const uint8_t code[] = {'"', 'a', 'A', '3', 'u'};
  assert(!isReaderSuccessReply(code, sizeof(code)));
  for (const auto& reply : replies) {
    std::vector<uint8_t> mixed(reply.begin(), reply.end());
    mixed.insert(mixed.end(), code, code + sizeof(code));
    assert(!isReaderSuccessReply(mixed.data(), mixed.size()));
    mixed.assign(code, code + sizeof(code));
    mixed.insert(mixed.end(), reply.begin(), reply.end());
    assert(!isReaderSuccessReply(mixed.data(), mixed.size()));
  }
  std::puts("Reader reply classification: PASS");
}
