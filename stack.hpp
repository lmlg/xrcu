#ifndef __XRCU_STACK_HPP__
#define __XRCU_STACK_HPP__   1

#include "xrcu.hpp"
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace std
{
  struct forward_iterator_tag;
}

namespace xrcu
{

namespace detail
{

struct stack_node_base
{
  stack_node_base *next;
  size_t size;
};

template <class T>
struct stack_node : public stack_node_base, finalizable
{
  T value;

  stack_node (const T& v) : value (v) {}

  void safe_destroy ()
    {
      delete this;
    }

  template <class ...Args>
  stack_node (Args&&... args) : value (std::forward<Args>(args)...) {}
};

struct stack_base
{
  alignas (alignof (size_t)) char buf[64];

  stack_base ();
  void destroy (void (*) (stack_node_base *));

  stack_node_base* root () const;

  void push_node (stack_node_base *nodep);
  stack_node_base* pop_node ();
  bool empty () const;
  size_t size () const;

  void swap (stack_base& right);
};

struct stack_iter_base : public cs_guard
{
  stack_node_base *runp;
  typedef std::forward_iterator_tag iterator_category;

  stack_iter_base (stack_node_base *rp) : runp (rp) {}

  stack_iter_base& operator++ ()
    {
      this->runp = this->runp->next;
      return (*this);
    }

  stack_iter_base operator++ (int)
    {
      stack_iter_base tmp (this->runp);
      return (++tmp);
    }

  bool operator== (const stack_iter_base& right) const
    {
      return (this->runp == right.runp);
    }

  bool operator!= (const stack_iter_base& right) const
    {
      return (this->runp != right.runp);
    }
};

} // namespace detail.

template <class T>
struct stack
{
  detail::stack_base stkbase;
  typedef detail::stack_node<T> node_type;

  stack () : stkbase () {}

  void push (const T& value)
    {
      this->stkbase.push_node (new node_type (value));
    }

  template <class ...Args>
  void emplace (Args&& ...args)
    {
      this->stkbase.push_node (new node_type (args...));
    }

  T pop ()
    {
      cs_guard g;
      auto node = (node_type *)this->stkbase.pop_node ();

      if (!node)
        throw std::runtime_error ("stack<T>::pop: stack is empty");

      T ret = node->value;
      finalize (node);
      return (ret);
    }

  T top ()
    {
      cs_guard g;
      auto node = (node_type *)this->stkbase.root ();

      if (!node)
        throw std::runtime_error ("stack<T>::top: stack is empty");

      return (node->value);
    }

  struct iterator : public detail::stack_iter_base
    {
      iterator (detail::stack_node_base *rp = nullptr) :
        detail::stack_iter_base (rp) {}

      T& operator* ()
        {
          return (((node_type *)this->runp)->value);
        }
    };

  struct const_iterator : public detail::stack_iter_base
    {
      const_iterator (detail::stack_node_base *rp = nullptr) :
        detail::stack_iter_base (rp) {}

      T operator* () const
        {
          return (((const node_type *)this->runp)->value);
        }
    };

  iterator begin ()
    {
      return (iterator (this->stkbase.root ()));
    }

  iterator end ()
    {
      return (iterator ());
    }

  const_iterator cbegin () const
    {
      return (const_iterator (this->stkbase.root ()));
    }

  const_iterator cend () const
    {
      return (const_iterator ());
    }

  const_iterator begin () const
    {
      return (this->cbegin ());
    }

  const_iterator end () const
    {
      return (this->cend ());
    }

  size_t size () const
    {
      return (this->stkbase.size ());
    }

  void swap (stack<T>& right)
    {
      if (this == &right)
        return;

      this->stkbase.swap (right.stkbase);
    }

  static void
  _Fini (detail::stack_node_base *ptr)
    {
      delete (node_type *)ptr;
    }

  ~stack ()
    {
      this->stkbase.destroy (_Fini);
    }
};

} // namespace xrcu

namespace std
{

template <class T>
void swap (xrcu::stack<T>& left, xrcu::stack<T>& right)
{
  return (left.swap (right));
}

} // namespace std

#endif
