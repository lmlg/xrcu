#ifndef __XRCU_OPTIONAL_HPP__
#define __XRCU_OPTIONAL_HPP__   1

#include <stdexcept>
#include <utility>

namespace xrcu
{

template <class T>
struct optional
{
  alignas (alignof (T)) char buf[sizeof (T)];
  bool valid = false;

  void _Init (const T& value)
    {
      new (&this->buf[0]) T (value);
      this->valid = true;
    }

  void _Init (T&& value)
    {
      new (&this->buf[0]) T (std::forward<T&&> (value));
      this->valid = true;
    }

  optional () = default;

  optional (const T& value)
    {
      this->_Init (value);
    }

  optional (const optional<T>& right)
    {
      if (right.valid)
        this->_Init (*right);
    }

  optional (T&& value)
    {
      this->_Init (static_cast<T&&> (value));
    }

  T& operator* ()
    {
      return (*(T *)(&this->buf[0]));
    }
  
  const T& operator* () const
    {
      return (*(const T *)(&this->buf[0]));
    }

  T* operator-> ()
    {
      return ((T *)&this->buf[0]);
    }

  const T* operator-> () const
    {
      return ((const T *)&this->buf[0]);
    }

  bool has_value () const
    {
      return (this->valid);
    }

  void reset ()
    {
      if (!this->valid)
        return;

      T *ptr = (T *)&this->buf[0];
      ptr->~T ();
      this->valid = false;
    }

  optional<T>& operator= (const optional<T>& right)
    {
      if (!right.valid)
        this->reset ();
      else if (!this->valid)
        this->_Init (*right);
      else
        **this = *right;

      return (*this);
    }

  optional<T>& operator= (const T& value)
    {
      if (!this->valid)
        this->_Init (value);
      else
        **this = value;

      return (*this);
    }

  optional<T>& operator= (T&& value)
    {
      this->reset ();
      this->_Init (std::forward<T&&> (value));
      return (*this);
    }
};

}   // namespace xrcu

#endif
