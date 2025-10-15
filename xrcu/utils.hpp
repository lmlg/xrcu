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

  void safe_destroy ()
    {
      destroy<T> (&this->value);
      dealloc_wrapped (this);
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
  typedef type_wrapper<T> wrapped_type;

  static uintptr_t make (const T& val)
    {
      auto rv = (wrapped_type *)alloc_wrapped (sizeof (wrapped_type));
      try
        {
          new (rv) wrapped_type (val);
          return ((uintptr_t)rv);
        }
      catch (...)
        {
          dealloc_wrapped (rv);
          throw;
        }
    }

  template <class ...Args>
  static uintptr_t make (Args&&... args)
    {
      auto rv = (wrapped_type *)alloc_wrapped (sizeof (wrapped_type));
      try
        {
          new (rv) wrapped_type (std::forward<Args>(args)...);
          return ((uintptr_t)rv);
        }
      catch (...)
        {
          dealloc_wrapped (rv);
          throw;
        }
    }

  static uintptr_t make (T&& val)
    {
      auto rv = (wrapped_type *)alloc_wrapped (sizeof (wrapped_type));
      try
        {
          new (rv) wrapped_type (std::forward<T&&> (val));
          return ((uintptr_t)rv);
        }
      catch (...)
        {
          dealloc_wrapped (rv);
          throw;
        }
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

} // namespace detail

} // namespace xrcu

#endif
