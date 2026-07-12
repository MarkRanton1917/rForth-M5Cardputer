#include <array>
#include <cstddef>

template<typename T, size_t L>
class RingList {
public:
  RingList()
    : head_(0),
      size_(0)
  {
  }

  T& operator[](size_t i)
  {
    size_t first = (size_ == L) ? head_ : 0;
    return data_[(first + i) % L];
  }

  const T& operator[](size_t i) const
  {
    size_t first = (size_ == L) ? head_ : 0;
    return data_[(first + i) % L];
  }

  void push(const T& value)
  {
    data_[head_] = value;
    head_ = (head_ + 1) % L;
    if (size_ < L) size_++;
  }

  T& newest()
  {
    if (size_ == 0) return data_[0];
    return data_[(head_ + L - 1) % L];
  }

  size_t size() const
  {
    return size_;
  }

private:
  std::array<T, L> data_;
  size_t head_;
  size_t size_;
};
