#ifndef GENERATOR_HPP
#define GENERATOR_HPP

#include <__coroutine/noop_coroutine_handle.h>
#include <coroutine>
#include <cstdint>
#include <exception>

// source:
//  i. https://en.cppreference.com/w/cpp/language/coroutines
//  ii. https://en.cppreference.com/w/cpp/coroutine/coroutine_handle (move assignment)
// todo: understand coroutines in more detail to make sure this class works as intended
template<typename T>
struct Generator
{
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type // required
  {
    T value_;
    std::exception_ptr exception_;

    Generator get_return_object()
    {
      return Generator(handle_type::from_promise(*this));
    }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { exception_ = std::current_exception(); }

    template<std::convertible_to<T> From>
    std::suspend_always yield_value(From&& from)
    {
      value_ = std::forward<From>(from);
      return {};
    }
    void return_void() {}
  };

  handle_type h_;

  Generator(handle_type h) : h_(h) {}
  Generator(Generator &&other)
    : h_(other.h_) {
    other.h_ = {};
  }
  Generator& operator=(Generator &&other) {
    if (this != &other) {
      if (h_) h_.destroy();
      h_ = other.h_;
      other.h_ = {};
    }
    return *this;
  }
  ~Generator() { if (h_) h_.destroy(); }
  explicit operator bool()
  {
    fill();
    return !h_.done();
  }
  T operator()()
  {
    fill();
    full_ = false;
    return std::move(h_.promise().value_);
  }
 
private:
  bool full_ = false;

  void fill()
  {
    if (!full_)
    {
      h_();
      if (h_.promise().exception_)
        std::rethrow_exception(h_.promise().exception_);
      full_ = true;
    }
  }
};

#endif
