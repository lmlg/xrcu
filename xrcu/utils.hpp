/* Declarations for miscelaneous utilities.

   This file is part of xrcu.

   xrcu is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#ifndef __XRCU_UTILS_HPP__
#define __XRCU_UTILS_HPP__   1

#include "xrcu.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace xrcu
{

namespace detail
{

void* alloc_wrapped (size_t size);
void dealloc_wrapped (void *ptr);

template <class T>
void destroy (void *ptr)
{
  ((T *)ptr)->~T ();
}

template <typename T>
struct alignas (8) type_wrapper_base : public finalizable
{
  T value;

  type_wrapper_base (const T& val) : value (val)
    {
    }

  template <typename ...Args>
  type_wrapper_base (Args&&... args) : value (std::forward<Args>(args)...)
    {
    }
};

template <typename T, typename Alloc>
struct alignas (8) type_wrapper : public type_wrapper_base<T>
{
  typedef type_wrapper<T, Alloc> Self;

  type_wrapper (const T& v) : type_wrapper_base<T> (v)
    {
    }

  template <typename ...Args>
  type_wrapper (Args&&... args) :
      type_wrapper_base<T> (std::forward<Args>(args)...)
    {
    }

  static Self* make (const T& val)
    {
      auto ret = Alloc().allocate (1);
      try
        {
          return (new (ret) Self (val));
        }
      catch (...)
        {
          Alloc().deallocate (ret, 1);
          throw;
        }
    }

  template <typename ...Args>
  static Self* make (Args&&... args)
    {
      auto ret = Alloc().allocate (1);
      return (new (ret) Self (std::forward<Args>(args)...));
    }

  void safe_destroy ()
    {
      destroy<T> (&this->value);
      Alloc().deallocate (this, 1);
    }
};

template <bool Integral, typename T, typename Alloc>
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

template <typename T, typename Alloc>
struct wrapped_traits<false, T, Alloc>
{
  static const uintptr_t XBIT = 1;
  static const uintptr_t FREE = 2;
  static const uintptr_t DELT = 4;
  typedef T value_type;

  using Nalloc = typename std::allocator_traits<Alloc>::template
                 rebind_alloc<type_wrapper_base<T>>;

  typedef type_wrapper<T, Nalloc> wrapped_type;
  static_assert (sizeof (wrapped_type) == sizeof (type_wrapper_base<T>),
                 "invalid size for type wrappers");

  static uintptr_t make (const T& val)
    {
      return ((uintptr_t)(wrapped_type::make (val)));
    }

  template <typename ...Args>
  static uintptr_t make (Args&&... args)
    {
      return ((uintptr_t)wrapped_type::make (std::forward<Args>(args)...));
    }

  static T& get (uintptr_t addr)
    {
      return (((wrapped_type *)(addr & ~XBIT))->value);
    }

  static void destroy (uintptr_t addr)
    {
      finalize ((wrapped_type *)addr);
    }

  static void free (uintptr_t addr)
    {
      ((wrapped_type *)addr)->safe_destroy ();
    }
};

static inline size_t upsize (size_t x)
{
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  if constexpr (sizeof (x) > sizeof (uint32_t))
    x |= x >> 32;

  return (x + 1);
}

template <typename I1, typename I2>
bool sequence_lt (I1 x1, I1 x2, I2 y1, I2 y2)
{
  for (; x1 != x2; ++x1, ++y1)
    {
      if (y1 == y2 || *y1 < *x1)
        return (false);
      else if (*x1 < *y1)
        return (true);
    }

  return (y1 != y2);
}

template <typename I1, typename I2>
bool sequence_eq (I1 x1, I1 x2, I2 y1, I2 y2)
{
  for (; x1 != x2 && y1 != y2; ++x1, ++y1)
    if (*x1 != *y1)
      return (false);

  return (x1 == x2 && y1 == y2);
}

} // namespace detail

} // namespace xrcu

#endif
