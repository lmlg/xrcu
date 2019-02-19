#ifndef __XRCU_OPTIONAL_HPP__
#define __XRCU_OPTIONAL_HPP__   1

#include <stdexcept>
#include <utility>

namespace xrcu
{

template <class T>
struct optional
{
  union _Optional
    {
      T value;
      char buf[sizeof (T)];

      T* _Ptr ()
        {
          return (&this->value);
        }

      const T* _Ptr () const
        {
          return (&this->value);
        }

      _Optional () {}
      ~_Optional () {}
    };

  _Optional optdata;
  bool valid = false;

  void _Init (const T& value)
    {
      new (this->optdata._Ptr ()) T (value);
      this->valid = true;
    }

  void _Init (T&& value)
    {
      new (this->optdata._Ptr ()) T (std::forward<T&&> (value));
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

  optional (optional<T>&& right)
    {
      if (right.valid)
        this->_Init (static_cast<T&&> (*right));
    }

  T& operator* ()
    {
      return (*this->optdata._Ptr ());
    }
  
  const T& operator* () const
    {
      return (*this->optdata._Ptr ());
    }

  T* operator-> ()
    {
      return (this->optdata._Ptr ());
    }

  const T* operator-> () const
    {
      return (this->optdata._Ptr ());
    }

  bool has_value () const
    {
      return (this->valid);
    }

  void reset ()
    {
      if (!this->valid)
        return;

      T *ptr = this->optdata._Ptr ();
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

  optional<T>& operator= (optional<T>&& right)
    {
      this->reset ();
      if (right.valid)
        this->_Init (std::forward<T&&> (*right));

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

  ~optional ()
    {
      this->reset ();
    }
};

}   // namespace xrcu

#endif
