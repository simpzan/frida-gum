#pragma once

#include <string>
#include <vector>
#include <unistd.h>

std::string bytes_to_hex_string(const std::vector<uint8_t> &input);


// A macro to disallow the copy constructor and operator= functions
// This must be placed in the private: declarations for a class.
//
// For disallowing only assign or copy, delete the relevant operator or
// constructor, for example:
// void operator=(const TypeName&) = delete;
// Note, that most uses of DISALLOW_ASSIGN and DISALLOW_COPY are broken
// semantically, one should either use disallow both or neither. Try to
// avoid these in new code.
//
// When building with C++11 toolchains, just use the language support
// for explicitly deleted methods.
#if __cplusplus >= 201103L
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
#else
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)
#endif


// Container for a file descriptor that automatically closes the descriptor as
// it goes out of scope.
//
//      unique_fd ufd(open("/some/path", "r"));
//      if (ufd.get() == -1) return error;
//
//      // Do something useful, possibly including 'return'.
//
//      return 0; // Descriptor is closed for you.
//
// unique_fd is also known as ScopedFd/ScopedFD/scoped_fd; mentioned here to help
// you find this class if you're searching for one of those names.
class unique_fd final {
 public:
  unique_fd() : value_(-1) {}
  explicit unique_fd(int value) : value_(value) {}
  ~unique_fd() { clear(); }
  unique_fd(unique_fd&& other) : value_(other.release()) {}
  unique_fd& operator=(unique_fd&& s) {
    reset(s.release());
    return *this;
  }
  void reset(int new_value) {
    if (value_ != -1) {
      // Even if close(2) fails with EINTR, the fd will have been closed.
      // Using TEMP_FAILURE_RETRY will either lead to EBADF or closing someone else's fd.
      // http://lkml.indiana.edu/hypermail/linux/kernel/0509.1/0877.html
      close(value_);
    }
    value_ = new_value;
  }
  void clear() {
    reset(-1);
  }
  int get() const { return value_; }
  int release() __attribute__((warn_unused_result)) {
    int ret = value_;
    value_ = -1;
    return ret;
  }
 private:
  int value_;
  DISALLOW_COPY_AND_ASSIGN(unique_fd);
};
