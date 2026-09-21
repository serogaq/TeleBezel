#pragma once
#include <unistd.h>
#include <utility>

namespace telebezel {
// Owns exactly one descriptor. A failed fsync/write must still release it.
class FileDescriptor final {
public:
  explicit FileDescriptor(int descriptor = -1) noexcept : descriptor_(descriptor) {}
  ~FileDescriptor() {
    if (descriptor_ >= 0)
      ::close(descriptor_);
  }
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  FileDescriptor(FileDescriptor &&other) noexcept : descriptor_(std::exchange(other.descriptor_, -1)) {}
  int get() const noexcept { return descriptor_; }
  int close() noexcept {
    const int value = std::exchange(descriptor_, -1);
    return value < 0 ? 0 : ::close(value);
  }

private:
  int descriptor_;
};
} // namespace telebezel
