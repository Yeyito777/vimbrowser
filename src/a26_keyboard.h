#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace vimbrowser {

// Moon owns the A26 software keyboard. This enum deliberately models only the
// fixed control protocol commands; callers cannot accidentally put page text or
// credentials on the shell socket.
enum class A26KeyboardPurpose {
  kHide,
  kText,
  kPassword,
  kSearch,
  kUrl,
  kNumber,
};

class A26KeyboardClient final {
 public:
  A26KeyboardClient();
  ~A26KeyboardClient();

  // Non-blocking for the caller. The single pending slot is intentionally
  // bounded and latest-wins so rapid focus changes cannot create an unbounded
  // queue or stale keyboard state.
  void Request(A26KeyboardPurpose purpose);

 private:
  void Run();

  std::mutex mutex_;
  std::condition_variable condition_;
  std::optional<A26KeyboardPurpose> pending_;
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace vimbrowser
