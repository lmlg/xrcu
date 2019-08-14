#ifndef __XRCU_OPTIONAL_HPP__
#define __XRCU_OPTIONAL_HPP__   1

#include <utility>

namespace xrcu
{

template <class T>
struct optional
{
  T *xptr;
  alignas (alignof (T)) char buf[sizeof (T)];

  T* _Ptr ()
    {
      return ((T *)&this->buf[0]);
    }

  const T* _Ptr () const
    {
      return ((const T *)&this->buf[0]);
    }

  void _Init (const T& value)
    {
      new (this->_Ptr ()) T (value);
      this->xptr = this->_Ptr ();
    }

  void _Init (T&& value)
    {
      new (this->_Ptr ()) T (std::forward<T&&> (value));
      this->xptr = this->_Ptr ();
    }

  optional () : xptr (nullptr)
    {
    }

  optional (const T& value)
    {
      this->_Init (value);
    }

  optional (const optional<T>& right)
    {
      if (right.xptr)
        this->_Init (*right);
    }

  optional (T&& value)
    {
      this->_Init (static_cast<T&&> (value));
    }

  optional (optional<T>&& right)
    {
      if (right.xptr)
        this->_Init (static_cast<T&&> (*right));
    }

  T& operator* ()
    {
      return (*this->xptr);
    }
  
  const T& operator* () const
    {
      return (*this->xptr);
    }

  T* operator-> ()
    {
      return (this->xptr);
    }

  const T* operator-> () const
    {
      return (this->xptr);
    }

  bool has_value () const
    {
      return (this->xptr != nullptr);
    }

  void reset ()
    {
      if (!this->xptr)
        return;

      this->xptr->~T ();
      this->xptr = nullptr;
    }

  optional<T>& operator= (const optional<T>& right)
    {
      if (!right.xptr)
        this->reset ();
      else if (!this->xptr)
        this->_Init (*right);
      else
        **this = *right;

      return (*this);
    }

  optional<T>& operator= (optional<T>&& right)
    {
      if (!right.xptr)
        this->reset ();
      else if (!this->xptr)
        this->_Init (std::forward<T&&> (*right));
      else
        **this = std::forward<T&&> (*right);

      return (*this);
    }

  optional<T>& operator= (const T& value)
    {
      if (!this->xptr)
        this->_Init (value);
      else
        **this = value;

      return (*this);
    }

  optional<T>& operator= (T&& value)
    {
      if (!this->xptr)
        this->_Init (std::forward<T&&> (value));
      else
        **this = std::forward<T&&> (value);

      return (*this);
    }

  ~optional ()
    {
      this->reset ();
    }
};

}   // namespace xrcu

#endif
