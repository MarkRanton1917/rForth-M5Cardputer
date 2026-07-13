#include <array>
#include <cstddef>
#include "Arduino.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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

  void pull()
  {
    if (size_ == 0) return;
    head_ = (head_ + L - 1) % L;
    size_--;
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

class KeyQueue {
public:
  KeyQueue()
    : ptr_(0),
      str_("")
  {
    sem_ = xSemaphoreCreateMutex();
  }

  void load(String str)
  {
    xSemaphoreTake(sem_, portMAX_DELAY);
    ptr_ = 0;
    str_ = str;
    xSemaphoreGive(sem_);
  }

  int pop()
  {
    xSemaphoreTake(sem_, portMAX_DELAY);
    int ret = -1;
    if (str_.length() > ptr_) ret = static_cast<unsigned char>(str_[ptr_++]);
    xSemaphoreGive(sem_);
    return ret;
  }

  size_t size() const
  {
    xSemaphoreTake(sem_, portMAX_DELAY);
    size_t s = str_.length() - ptr_;
    xSemaphoreGive(sem_);
    return s;
  }

private:
  size_t ptr_;
  String str_;
  SemaphoreHandle_t sem_;
};
