#ifndef __XRCU_UTILS_HPP__
#define __XRCU_UTILS_HPP__   1

#include "xrcu.hpp"
#include <utility>

namespace xrcu
{

namespace detail
{

template <class T>
struct alignas (8) type_wrapper : public finalizable
{
  T value;

  type_wrapper (const T& val) : value (val)
    {
    }

  template <class ...Args>
  type_wrapper (Args&&... args) : value (std::forward<Args>(args)...)
    {
    }
};

template <bool Integral, class T>
struct wrapped_traits
{
  static const uintptr_t XBIT = (uintptr_t)1 << (sizeof (uintptr_t) * 8 - 1);
  static const uintptr_t FREE = (~(uintptr_t)0) & ~XBIT;
  static const uintptr_t DELT = FREE >> 1;
  typedef T value_type;

  static uintptr_t make (T val)
    {
      return ((uintptr_t)(typename std::make_unsigned<T>::type)val);
    }

  static T get (uintptr_t w)
    {
      return ((T)(w & ~XBIT));
    }

  static void destroy (uintptr_t) {}
  static void free (uintptr_t) {}
};

template <class T>
struct wrapped_traits<false, T>
{
  static const uintptr_t XBIT = 1;
  static const uintptr_t FREE = 2;
  static const uintptr_t DELT = 4;
  typedef T value_type;

  static uintptr_t make (const T& val)
    {
      return ((uintptr_t)(new type_wrapper<T> (val)));
    }

  template <class ...Args>
  static uintptr_t make (Args&&... args)
    {
      return ((uintptr_t)(new type_wrapper<T> (std::forward<Args>(args)...)));
    }

  static uintptr_t make (T&& val)
    {
      return ((uintptr_t)(new type_wrapper<T> (std::forward<T&&> (val))));
    }

  static T& get (uintptr_t addr)
    {
      return (((type_wrapper<T> *)(addr & ~XBIT))->value);
    }

  static void destroy (uintptr_t addr)
    {
      finalize ((type_wrapper<T> *)addr);
    }

  static void free (uintptr_t addr)
    {
      delete (type_wrapper<T> *)addr;
    }
};

} // namespace detail

} // namespace xrcu

#endif
