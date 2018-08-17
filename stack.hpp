#ifndef __XRCU_STACK_HPP__
#define __XRCU_STACK_HPP__   1

#include "xrcu.hpp"
#include <cstddef>
#include <stdexcept>

namespace std
{
  struct forward_iterator_tag;
}

namespace xrcu
{

struct stack_node_base : public finalizable
{
  stack_node_base *next;
};

template <class T>
struct stack_node : public stack_node_base
{
  T value;

  stack_node (const T& v) : value (v) {}
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
};

struct stack_iter_base : public cs_guard
{
  stack_node_base *runp;
  typedef std::forward_iterator_tag iterator_category;

  stack_iter_base (stack_node_base *rp) : runp (rp) {}

  stack_iter_base& operator++ ()
    {
      this->runp = this->runp->next;
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

template <class T>
struct stack : public stack_base
{
  typedef stack_node<T> node_type;

  void push (const T& value)
    {
      node_type *nodep = new node_type (value);
      this->push_node (nodep);
    }

  T pop ()
    {
      cs_guard g;
      node_type *node = (node_type *)this->pop_node ();

      if (!node)
        throw std::runtime_error ("stack<T>::pop: stack is empty");

      T ret = node->value;
      finalize (node);
      return (ret);
    }

  T top ()
    {
      cs_guard g;
      node_type *node = (node_type *)this->root ();

      if (!node)
        throw std::runtime_error ("stack<T>::top: stack is empty");

      return (node->value);
    }

  struct iterator : public stack_iter_base
    {
      iterator (stack_node_base *rp = nullptr) : stack_iter_base (rp) {}

      T& operator* ()
        {
          return (((node_type *)this->runp)->value);
        }
    };

  struct const_iterator : public stack_iter_base
    {
      const_iterator (stack_node_base *rp = nullptr) : stack_iter_base (rp) {}

      T operator* () const
        {
          return (((const node_type *)this->runp)->value);
        }
    };

  iterator begin ()
    {
      return (iterator (this->root ()));
    }

  iterator end ()
    {
      return (iterator ());
    }

  const_iterator cbegin () const
    {
      return (const_iterator (this->root ()));
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

  static void
  fini_node (stack_node_base *ptr)
    {
      delete (node_type *)ptr;
    }

  ~stack ()
    {
      this->destroy (fini_node);
    }
};

}

#endif
