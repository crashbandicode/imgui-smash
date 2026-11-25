#include "shared_fonts.hpp"

#include "font_feed.hpp"
#include "helpers/memoryHelper.h"
#include "nn/os.hpp"
#include "nn/pl.h"

namespace ImguiSharedFonts {
namespace {

constexpr nn::pl::SharedFontType kOrder[ImguiFontFeed::kTypes] = {
    nn::pl::STANDARD,     nn::pl::CHINESE_SIMPLIFIED, nn::pl::EXT_CHINESE_SIMPLIFIED,
    nn::pl::CHINESE_TRAD, nn::pl::KOREAN,             nn::pl::NN_EXT,
};
constexpr size_t kStackSize = 0x4000;
constexpr int kPollMilliseconds = 100;

void request(int type) { nn::pl::RequestSharedFontLoad(kOrder[type]); }

bool loaded(int type) {
  return nn::pl::GetSharedFontLoadState(kOrder[type]) == nn::pl::SharedFontLoadState_Loaded;
}

const void *address(int type) { return nn::pl::GetSharedFontAddress(kOrder[type]); }

size_t size(int type) { return nn::pl::GetSharedFontSize(kOrder[type]); }

bool sleep() {
  nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(kPollMilliseconds));
  return true;
}

bool startThread(void (*entry)(void *), void *arg) {
  // From the host allocator, not static storage. Kept for the session: the thread object must outlive the thread.
  auto *thread = static_cast<nn::os::ThreadType *>(Mem::AllocateAlign(0x10, sizeof(nn::os::ThreadType)));
  void *stack = Mem::AllocateAlign(0x1000, kStackSize);
  // The form without a core number: the thread runs on the system's default core, at the lowest priority.
  if (thread == nullptr || stack == nullptr ||
      nn::os::CreateThread(thread, entry, arg, stack, kStackSize, nn::os::LowestThreadPriority) != 0) {
    if (stack != nullptr) {
      Mem::Deallocate(stack);
    }
    if (thread != nullptr) {
      Mem::Deallocate(thread);
    }
    return false;
  }
  nn::os::StartThread(thread);
  return true;
}

const ImguiFontFeed::Platform kPlatform = {request, loaded, address, size, sleep, startThread};

}  // namespace

bool start() {
  ImguiFontFeed::reset();
  return ImguiFontFeed::startWorker(&kPlatform);
}

}  // namespace ImguiSharedFonts
