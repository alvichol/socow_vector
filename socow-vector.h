#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <utility>

template <typename T, std::size_t SMALL_SIZE>
class socow_vector {
public:
  using value_type = T;

  using reference = T&;
  using const_reference = const T&;

  using pointer = T*;
  using const_pointer = const T*;

  using iterator = pointer;
  using const_iterator = const_pointer;

private:
  class buffer {
  public:
    std::size_t capacity_;
    std::size_t ref_count_;
    T data_[0];

    std::size_t capacity() const noexcept {
      return capacity_;
    }

    T* data() noexcept {
      return data_;
    }

    static buffer* create_buf(std::size_t capacity) {
      buffer* new_buf = static_cast<buffer*>(operator new(sizeof(buffer) + sizeof(T) * capacity));
      new (new_buf) buffer(capacity);
      return new_buf;
    }

  private:
    explicit buffer(std::size_t capacity) noexcept : capacity_(capacity), ref_count_(1) {}
  };

private:
  std::size_t size_;
  bool is_small_;

  union {
    buffer* buf_;
    T small_[SMALL_SIZE];
  };

public:
  socow_vector() noexcept : size_(0), is_small_(true) {}

  socow_vector(const socow_vector& other) : socow_vector() {
    *this = other;
  }

  socow_vector& operator=(const socow_vector& other) {
    if (this == &other) {
      return *this;
    }
    if (!other.is_small_) {
      container_destroy();
      buf_ = other.buf_;
      buf_->ref_count_++;
      is_small_ = false;
    } else if (!is_small_ && other.is_small_) {
      copy_to_small(other.begin(), other.size());
    } else {
      std::size_t min_size = std::min(size(), other.size());
      socow_vector tmp;
      std::uninitialized_copy_n(other.begin(), min_size, tmp.begin());
      tmp.size_ = min_size;

      if (size() < other.size()) {
        std::uninitialized_copy(other.begin() + size(), other.end(), end());
      } else {
        std::destroy(begin() + other.size(), end());
      }
      size_ = other.size();
      std::swap_ranges(begin(), begin() + min_size, tmp.begin());
    }
    size_ = other.size();
    return *this;
  }

  void release_ref(buffer* buf) noexcept {
    buf->ref_count_--;
    if (buf->ref_count_ == 0) {
      std::destroy_n(buf->data(), size());
      buf->~buffer();
      operator delete(buf);
    }
  }

  void container_destroy() noexcept {
    if (is_small_) {
      std::destroy_n(begin(), size());
    } else {
      release_ref(buf_);
    }
  }

  ~socow_vector() noexcept {
    container_destroy();
  }

  std::size_t size() const noexcept {
    return size_;
  }

  std::size_t capacity() const noexcept {
    return is_small_ ? SMALL_SIZE : buf_->capacity();
  }

  pointer data() {
    if (!shared_buf()) {
      return is_small_ ? small_ : buf_->data();
    }
    unshare(capacity());
    return buf_->data();
  }

  const_pointer data() const noexcept {
    return is_small_ ? small_ : buf_->data();
  }

  iterator begin() {
    return data();
  }

  iterator end() {
    return data() + size();
  }

  const_iterator begin() const noexcept {
    return data();
  }

  const_iterator end() const noexcept {
    return data() + size();
  }

  reference operator[](std::size_t index) {
    return data()[index];
  }

  const_reference operator[](std::size_t index) const noexcept {
    return data()[index];
  }

  reference front() {
    return data()[0];
  }

  const_reference front() const noexcept {
    return data()[0];
  }

  reference back() {
    return data()[size() - 1];
  }

  const_reference back() const noexcept {
    return data()[size() - 1];
  }

  bool empty() const noexcept {
    return size() == 0;
  }

private:
  // this ctor doesn't create small object even if capacity <= SMALL_SIZE
  explicit socow_vector(std::size_t capacity) : size_(0), is_small_(false) {
    buf_ = buffer::create_buf(capacity);
  }

  bool shared_buf() noexcept {
    return !is_small_ && buf_->ref_count_ > 1;
  }

  void copy_to_small(const_iterator from, std::size_t size) {
    buffer* tmp = buf_;
    try {
      std::uninitialized_copy_n(from, size, small_);
    } catch (...) {
      buf_ = tmp;
      throw;
    }
    is_small_ = true;
    release_ref(tmp);
  }

  void unshare(std::size_t new_capacity) {
    buffer* new_buf = buffer::create_buf(new_capacity);
    try {
      std::uninitialized_copy_n(std::as_const(*this).data(), size(), new_buf->data());
    } catch (...) {
      new_buf->~buffer();
      operator delete(new_buf);
      throw;
    }
    container_destroy();
    is_small_ = false;
    buf_ = new_buf;
  }

public:
  void push_back(const T& value) {
    insert(std::as_const(*this).end(), value);
  }

  void pop_back() {
    erase(std::as_const(*this).end() - 1);
  }

  void reserve(std::size_t new_capacity) {
    if (new_capacity > size()) {
      if (shared_buf() && new_capacity <= SMALL_SIZE) {
        copy_to_small(buf_->data(), size());
      } else if (shared_buf() || new_capacity > capacity()) {
        unshare(new_capacity);
      }
    }
  }

  void shrink_to_fit() {
    if (!is_small_ && capacity() > size()) {
      if (size() <= SMALL_SIZE) {
        copy_to_small(buf_->data(), size());
      } else {
        unshare(size());
      }
    }
  }

  void clear() noexcept {
    if (shared_buf()) {
      release_ref(buf_);
      is_small_ = true;
    } else {
      std::destroy_n(begin(), size());
    }
    size_ = 0;
  }

  void swap(socow_vector& other) {
    if (this == &other) {
      return;
    }
    if (size() > other.size()) {
      other.swap(*this);
      return;
    }

    if (is_small_ && other.is_small_) {
      std::uninitialized_copy(other.begin() + size(), other.end(), end());
      std::destroy(other.begin() + size(), other.end());
      std::swap(size_, other.size_);
      std::swap_ranges(begin(), begin() + other.size(), other.begin());
      return;
    }

    socow_vector tmp(other);
    other = *this;
    *this = tmp;
  }

  iterator insert(const_iterator pos, const T& value) {
    std::ptrdiff_t diff = pos - std::as_const(*this).begin();
    if (size() == capacity() || shared_buf()) {
      socow_vector tmp(size() != capacity() ? capacity() : capacity() * 2);
      std::uninitialized_copy_n(std::as_const(*this).begin(), diff, tmp.begin());
      tmp.size_ = diff;
      new (tmp.end()) T(value);
      tmp.size_++;
      std::uninitialized_copy(std::as_const(*this).begin() + diff, std::as_const(*this).end(), tmp.end());
      tmp.size_ = size() + 1;
      *this = tmp;
    } else {
      new (end()) T(value);
      size_++;
      std::size_t idx = size() - 1;
      while (idx > diff) {
        std::swap(data()[idx], data()[idx - 1]);
        --idx;
      }
    }
    return begin() + diff;
  }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    std::ptrdiff_t left = first - std::as_const(*this).begin();
    std::ptrdiff_t right = last - std::as_const(*this).begin();
    std::ptrdiff_t diff = left;
    if (left == right) {
      return begin() + diff;
    }
    if (shared_buf()) {
      socow_vector tmp(capacity());
      std::uninitialized_copy_n(buf_->data(), left, tmp.begin());
      tmp.size_ = left;
      std::uninitialized_copy_n(buf_->data() + right, size() - right, tmp.begin() + left);
      tmp.size_ = size() - (right - left);
      swap(tmp);
      return begin() + diff;
    }
    while (right < size()) {
      std::swap(data()[left], data()[right]);
      ++left;
      ++right;
    }
    std::destroy(end() - right + left, end());
    size_ -= right - left;
    return begin() + diff;
  }
};
