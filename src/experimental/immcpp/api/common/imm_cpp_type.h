#ifndef __IMM_CPP_TYPE__
#define __IMM_CPP_TYPE__

#include <saAis.h>
#include <string.h>
#include <iostream>
#include "base/osaf_extended_name.h"

struct cpptime_t {
  cpptime_t() {
    data_ = 0;
  }

  cpptime_t(SaTimeT i) {
    data_ = i;
  }

  cpptime_t(int i) {
    data_ = static_cast<SaTimeT>(i);
  }

  cpptime_t(long int i) {
    data_ = static_cast<SaTimeT>(i);
  }

  cpptime_t& operator=(const SaTimeT& i) {
    data_ = i;
    return *this;
  }

  cpptime_t& operator=(const cpptime_t& i) {
    data_ = i.data_;
    return *this;
  }

  bool operator==(const cpptime_t& i) {
    return data_ == i.data_;
  }

  bool operator==(SaTimeT& i) {
    return data_ == i;
  }

  friend bool operator==(const SaTimeT& i, const cpptime_t& ii) {
    return i == ii.data_;
  }

  friend bool operator==(SaTimeT& i, cpptime_t& ii) {
    return i == ii.data_;
  }

  bool operator==(int i) {
    return data_ == static_cast<SaTimeT>(i);
  }

  SaTimeT operator()() {
    return data_;
  }

  SaTimeT* operator&() { return &data_; };

  SaTimeT data_;
};

class base_t {
 public:
  virtual void* data() = 0;
  // to be able to deleted inherit classes
  virtual ~base_t() {};
  virtual void display() = 0;
};

template<typename T>
class numeric_t : public base_t {
 public:
  numeric_t() {
    data_ = 0;
  }

  numeric_t(T i) {
    data_ = i;
  }

  numeric_t(SaDoubleT i) {
    data_ = static_cast<SaDoubleT>(i);
  }

  numeric_t(int i) {
    data_ = static_cast<T>(i);
  }

  numeric_t(long int i) {
    data_ = static_cast<T>(i);
  }

  numeric_t(numeric_t<T>& i) {
    data_ = i.data_;
  }

  numeric_t(const numeric_t<T>& i) {
    data_ = i.data_;
  }

  bool operator==(const T& i) const {
    return data_ == i;
  }

  const T operator()() const {
    return data_;
  }

  void* data() { return &data_; };

  void display() {
    std::cout << "\tdata: " << data_ << std::endl;
  }

 private:
  T data_;
};

template<>
class numeric_t<SaInt32T> : public base_t {
 public:
  numeric_t() {
    data_ = 0;
  }

  numeric_t(SaInt32T i) {
    data_ = i;
  }

  numeric_t(numeric_t<SaInt32T>& i) {
    data_ = i.data_;
  }

  numeric_t(const numeric_t<SaInt32T>& i) {
    data_ = i.data_;
  }

  bool operator==(const SaInt32T& i) const {
    return data_ == i;
  }

  const SaInt32T operator()() const {
    return data_;
  }

  void* data() { return &data_; };

  void display() {
    std::cout << "\tdata: " << data_ << std::endl;
  }


 private:
  SaInt32T data_;
};

template<>
class numeric_t<SaDoubleT> : public base_t {
 public:
  numeric_t() {
    data_ = 0;
  }

  numeric_t(SaDoubleT i) {
    data_ = i;
  }

  numeric_t(numeric_t<SaDoubleT>& i) {
    data_ = i.data_;
  }

  numeric_t(const numeric_t<SaDoubleT>& i) {
    data_ = i.data_;
  }

  bool operator==(const SaDoubleT& i) const {
    return data_ == i;
  }

  const SaDoubleT operator()() const {
    return data_;
  }

  void* data() { return &data_; };

  void display() {
    std::cout << "\tdata: " << data_ << std::endl;
  }

 private:
  SaDoubleT data_;
};

class string_t : public base_t {
public:
  string_t() {
    data_ = nullptr;
  }

  string_t(const char* i) {
    data_ = new char[strlen(i) + 1]();
    memcpy(data_, i, strlen(i) + 1);
  }

  string_t(const std::string& i) {
    data_ = new char[i.length() + 1]();
    memcpy(data_, i.c_str(), i.length());
  }

  string_t(const string_t& i) {
    if (i.data_ == nullptr) return;
    data_ = new char[strlen(i.data_) + 1]();
    memcpy(data_, i(), strlen(i.data_) + 1);
  }

  string_t(string_t& i) {
    if (i.data_ == nullptr) return;
    data_ = new char[strlen(i.data_) + 1]();
    memcpy(data_, i(), strlen(i.data_) + 1);
  }

  // string_t& operator=(string i) {
  //   std::swap(data_, i.data_);
  //   return *this;
  // }

  ~string_t() {
    if (data_ != nullptr) {
      delete[] data_;
      data_ = nullptr;
    }
  }

  const char* operator()() const {
    return data_;
  }

  void* data() { return &data_; };

  void display() {
    std::cout << "\tdata: " << data_ << std::endl;
  }

private:
  char* data_;
};

template<> inline void numeric_t<cpptime_t>::display() {
  std::cout << "\tdata: " << data_() << std::endl;
}

class name_t : public base_t {
 public:
  name_t() {
    buf_ = NULL;
  }

  name_t(name_t& i) {
    buf_ = new char[strlen(i.buf_) + 1]();
    memcpy(buf_, i.buf_, strlen(i.buf_) + 1);
    osaf_extended_name_lend(buf_, &data_);
  }

  name_t(SaNameT& i) {
    SaConstStringT n = osaf_extended_name_borrow(&i);
    buf_ = new char[strlen(n) + 1]();
    memcpy(buf_, n, strlen(n) + 1);
    osaf_extended_name_lend(buf_, &data_);
  }

  name_t(const name_t& i) {
    buf_ = new char[strlen(i.buf_) + 1]();
    memcpy(buf_, i.buf_, strlen(i.buf_) + 1);
    osaf_extended_name_lend(buf_, &data_);
  }

  name_t(std::string& i) {
    buf_ = new char[i.length() + 1]();
    memcpy(buf_, i.c_str(), i.length() + 1);
    osaf_extended_name_lend(buf_, &data_);
  }

  name_t(const std::string& i) {
    buf_ = new char[i.length() + 1]();
    memcpy(buf_, i.c_str(), i.length() + 1);
    osaf_extended_name_lend(buf_, &data_);
  }

  ~name_t() {
    if (buf_ != nullptr) {
      delete[] buf_;
      buf_ = nullptr;
    }
  }

  const char* operator()() {
    return buf_;
  }

  void* data() { return &data_; };

  void display() {
    std::cout << "\tdata: " << buf_ << std::endl;
  }

 private:
  char* buf_;
  SaNameT data_;
};

class any_t : public base_t {
 public:
  any_t() {
    memset(&data_, 0, sizeof(SaAnyT));
  }

  ~any_t() {
    if (data_.bufferAddr != nullptr) {
      delete[] data_.bufferAddr;
      data_.bufferAddr = nullptr;
    }
  }

  explicit any_t(const SaAnyT& i) {
    data_.bufferSize = i.bufferSize;
    data_.bufferAddr = new SaUint8T[i.bufferSize]();
    memcpy((char*)data_.bufferAddr, i.bufferAddr, i.bufferSize);
  }

  explicit any_t(const char* i, size_t size) {
    data_.bufferSize = size;
    data_.bufferAddr = new SaUint8T[size]();
    memcpy((char*)data_.bufferAddr, i, size);
  }

  explicit any_t(const char* i) : any_t{i, strlen(i)} { }
  explicit any_t(const std::string& i) : any_t{i.c_str()} { }

  const SaAnyT* operator()() {
    return &data_;
  }

  void* data() { return &data_; };

  void display() {
    std::cout << "\tdata: " << data_.bufferAddr << std::endl;
  }


 private:
  SaAnyT data_;
};

using i32_t = numeric_t<SaInt32T>;
using u32_t = numeric_t<SaUint32T>;
using i64_t = numeric_t<SaInt64T>;
using u64_t = numeric_t<SaUint64T>;
using f64_t = numeric_t<SaFloatT>;
using d64_t = numeric_t<SaDoubleT>;
using t64_t = numeric_t<cpptime_t>;


#endif
