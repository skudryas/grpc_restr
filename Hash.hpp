#include <unordered_map>
#include <string_view>

#include "murmur3.h"

// 1. hash +
// 2. write flush ?
// 3. fast async ?
// 4. use Conn wrappers!

struct MurMurHasher32 {
  using result_type = uint32_t;
  result_type operator() (const std::string_view& s) {
    result_type ret;
    MurmurHash3_x86_32(s.data(), s.size(), 0, (void*)&ret);
    return ret;
  }
};

using StdHasher = std::hash<std::string_view>;

template <typename Hasher>
class BaseHash {
  private:
    typename Hasher::result_type value_;
  public:
    BaseHash(typename Hasher::result_type val): value_(val) {}
    BaseHash(): value_(0) {}
    BaseHash(const std::string &s): value_(Hasher{}(std::string_view(s))) {}
    BaseHash(const std::string_view &s): value_(Hasher{}(s)) {}
    BaseHash(const char *s): value_(Hasher{}(std::string_view(s))) {}
    typename Hasher::result_type value() const { return value_; }
    operator typename Hasher::result_type() { return value_; }
    bool operator==(const BaseHash& rhs) const { return value_ == rhs.value_; }
};

using Hash = BaseHash<StdHasher>;
//using Hash = BaseHash<MurMurHasher32>;
 
namespace std {

template <typename T>
struct hash<BaseHash<T>>
{
  BaseHash<T> operator()(const BaseHash<T>& value) const { return value; }
};

} // namespace std

