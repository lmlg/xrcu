#ifndef __XRCU_STACK_HPP__
#define __XRCU_STACK_HPP__   1

#include "xrcu.hpp"
#include <cstddef>

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
  alignas (64) char buf[64];

  void push_node (stack_node_base *nodep);
  stack_node_base* pop_node ();
  bool empty () const;
  size_t size () const;
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
      node_type node = this->pop_node ();

      if (!node)
        throw std::runtime_error ("stack<T>::pop: stack is empty");

      T ret = node->value;
      delete node;
      return (ret);
    }
};

}

#endif
