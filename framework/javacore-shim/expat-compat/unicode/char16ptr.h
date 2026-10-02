#pragma once
#include <stdint.h>
namespace icu {
class Char16Ptr {
  public:
    explicit Char16Ptr(uint16_t* p) : p_(reinterpret_cast<char16_t*>(p)) {}
    char16_t* get() const { return p_; }
  private:
    char16_t* p_;
};
}  // namespace icu
