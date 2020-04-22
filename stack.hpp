/* Declarations for the stack template type.

   This file is part of xrcu.

   khipu is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#ifndef __XRCU_STACK_HPP__
#define __XRCU_STACK_HPP__   1

#include "xrcu.hpp"
#include "optional.hpp"
#include <atomic>
#include <cstddef>
#include <utility>
#include <initializer_list>

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
  stack_node_base *next = nullptr;
  typedef std::atomic<stack_node_base *> ptr_type;

  static stack_node_base* root (const ptr_type& head);

  static void push (ptr_type& head, stack_node_base *nodep);

  static void push (ptr_type& head,
    stack_node_base *nodep, stack_node_base **outp);

  static stack_node_base* pop (ptr_type& head);

  static void swap (ptr_type& h1, ptr_type& h2);

  static stack_node_base* clear (ptr_type& head);

  static size_t size (const ptr_type& head);
};

struct stack_iter_base : public cs_guard
{
  stack_node_base *runp;

  typedef std::forward_iterator_tag iterator_category;
  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  stack_iter_base (stack_node_base *rp) : runp (rp)
    {
    }

  stack_iter_base (const stack_iter_base& right) : runp (right.runp)
    {
    }

  stack_iter_base (stack_iter_base&& right) : runp (right.runp)
    {
      right.runp = nullptr;
    }

  void _Adv ()
    {
      this->runp = this->runp->next;
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
  std::atomic<detail::stack_node_base *> hnode;

  struct _Stknode : public detail::stack_node_base, finalizable
    {
      T value;

      _Stknode (const T& v) : value (v)
        {
        }

      _Stknode (T&& v) : value (std::move (v))
        {
        }

      template <class ...Args>
      _Stknode (Args&&... args) : value (std::forward<Args>(args)...)
        {
        }

      void safe_destroy ()
        {
          auto runp = this->next;
          delete this;
          stack<T>::_Clean_nodes (runp);
        }
    };

  typedef _Stknode node_type;
  typedef T value_type;
  typedef T& reference;
  typedef const T& const_reference;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  void _Reset (detail::stack_node_base *ptr)
    {
      this->hnode.store (ptr, std::memory_order_relaxed);
    }

  static void _Clean_nodes (detail::stack_node_base *runp)
    {
      while (runp)
        {
          auto tmp = runp->next;
          delete (node_type *)runp;
          runp = tmp;
        }
    }

  template <class T1>
  void _Init (T1 first, T1 last, std::false_type)
    {
      detail::stack_node_base *runp = nullptr, **outp = &runp;
      try
        {
          for (; first != last; ++first)
            {
              *outp = new node_type (*first);
              outp = &(*outp)->next;
            }
        }
      catch (...)
        {
          _Clean_nodes (runp);
          throw;
        }

      this->_Reset (runp);
    }

  template <class T1, class T2>
  void _Init (T1 n, const T2& value, std::true_type)
    {
      detail::stack_node_base *runp = nullptr, **outp = &runp;

      try
        {
          for (T1 x = 0; x != n; ++x)
            {
              *outp = new node_type (value);
              outp = &(*outp)->next;
            }
        }
      catch (...)
        {
          _Clean_nodes (runp);
          throw;
        }

      this->_Reset (runp);
    }

  stack ()
    {
      this->_Reset (nullptr);
    }

  template <class T1, class T2>
  stack (T1 first, T2 last)
    {
      this->_Reset (nullptr);
      this->_Init (first, last, typename std::is_integral<T1>::type ());
    }

  stack (std::initializer_list<T> lst) : stack (lst.begin (), lst.end ())
    {
    }

  stack (const stack<T>& right) : stack (right.begin (), right.end ())
    {
    }

  stack (stack<T>&& right)
    {
      this->_Reset (right._Root ());
      right._Reset (nullptr);
    }

  void push (const T& value)
    {
      cs_guard g;
      detail::stack_node_base::push (this->hnode, new node_type (value));
    }

  void push (T&& value)
    {
      cs_guard g;
      detail::stack_node_base::push (this->hnode,
        new node_type (std::move (value)));
    }

  template <class Iter>
  void _Push (Iter first, Iter last, std::false_type)
    {
      if (first == last)
        return;

      node_type *np;

      try
        {
          np = new node_type (*first);
          auto **outp = &np->next;

          for (++first; first != last; ++first)
            {
              node_type *tmp = new node_type (*first);
              *outp = tmp, outp = &tmp->next;
            }

          detail::stack_node_base::push (this->hnode, np, outp);
        }
      catch (...)
        {
          _Clean_nodes (np);
          throw;
        }
    }

  template <class T1, class T2>
  void _Push (T1 n, const T2& value, std::true_type)
    {
      if (n == 0)
        return;

      node_type *np;

      try
        {
          np = new node_type (value);
          auto **outp = &np->next;

          for (; n != 0; --n)
            {
              node_type *tmp = new node_type (value);
              *outp = tmp, outp = &tmp->next;
            }

          detail::stack_node_base::push (this->hnode, np, outp);
        }
      catch (...)
        {
          _Clean_nodes (np);
          throw;
        }
    }

  template <class T1, class T2>
  void push (T1 first, T2 last)
    {
      this->_Push (first, last, typename std::is_integral<T1>::type ());
    }

  template <class ...Args>
  void emplace (Args&& ...args)
    {
      detail::stack_node_base::push (this->hnode,
        new node_type (std::forward<Args>(args)...));
    }

  optional<T> pop ()
    {
      cs_guard g;
      auto node = detail::stack_node_base::pop (this->hnode);

      if (!node)
        return (optional<T> ());

      optional<T> ret { ((node_type *)node)->value };
      finalize ((node_type *)node);
      return (ret);
    }

  node_type* _Root ()
    {
      return ((node_type *)detail::stack_node_base::root (this->hnode));
    }

  node_type* _Root () const
    {
      return ((node_type *)detail::stack_node_base::root (this->hnode));
    }

  optional<T> top ()
    {
      cs_guard g;
      auto node = this->_Root ();
      return (node ? optional<T> { node->value } : optional<T> ());
    }

  struct iterator : public detail::stack_iter_base
    {
      typedef T value_type;
      typedef T& reference;
      typedef T* pointer;

      iterator (detail::stack_node_base *rp = nullptr) :
          detail::stack_iter_base (rp)
        {
        }

      iterator (const iterator& it) : detail::stack_iter_base (it)
        {
        }

      iterator (iterator&& it) : detail::stack_iter_base (std::move (it))
        {
        }

      T& operator* ()
        {
          return (((node_type *)this->runp)->value);
        }

      const T& operator* () const
        {
          return (((const node_type *)this->runp)->value);
        }

      T* operator-> ()
        {
          return (&**this);
        }

      const T* operator-> () const
        {
          return (&**this);
        }

      iterator& operator++ ()
        {
          this->_Adv ();
          return (*this);
        }

      iterator operator++ (int)
        {
          iterator tmp = { this->runp };
          this->_Adv ();
          return (tmp);
        }
    };

  typedef const iterator const_iterator;

  iterator begin ()
    {
      return (iterator (this->_Root ()));
    }

  iterator end ()
    {
      return (iterator ());
    }

  const_iterator cbegin () const
    {
      return (const_iterator (this->_Root ()));
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
      cs_guard g;
      return (detail::stack_node_base::size (this->hnode));
    }

  size_t max_size () const
    {
      return (~(size_t)0);
    }

  bool empty () const
    {
      return (this->_Root () == nullptr);
    }

  void swap (stack<T>& right)
    {
      cs_guard g;

      if (this != &right)
        detail::stack_node_base::swap (this->hnode, right.hnode);
    }

  stack<T>& operator= (const stack<T>& right)
    {
      cs_guard g;

      if (this != &right)
        this->assign (right.begin (), right.end ());

      return (*this);
    }

  stack<T>& operator= (stack<T>&& right)
    {
      this->swap (right);
      finalize (right._Root ());
      right._Reset (nullptr);
      return (*this);
    }

  bool operator== (const stack<T>& right) const
    {
      auto x1 = this->cbegin (), x2 = this->cend ();
      auto y1 = right.cbegin (), y2 = right.cend ();

      for (; x1 != x2 && y1 != y2; ++x1, ++y1)
        if (*x1 != *y1)
          return (false);

      return (x1 == x2 && y1 == y2);
    }

  bool operator!= (const stack<T>& right) const
    {
      return (!(*this == right));
    }

  bool operator< (const stack<T>& right) const
    {
      auto x1 = this->cbegin (), x2 = this->cend ();
      auto y1 = right.cbegin (), y2 = right.cend ();

      for (; x1 != x2; ++x1, ++y1)
        {
          if (y1 == y2 || *y1 < *x1)
            return (false);
          else if (*x1 < *y1)
            return (true);
        }

      return (y1 != y2);
    }

  bool operator> (const stack<T>& right) const
    {
      return (right < *this);
    }

  bool operator<= (const stack<T>& right) const
    {
      return (!(right < *this));
    }

  bool operator>= (const stack<T>& right) const
    {
      return (!(*this < right));
    }

  void clear ()
    {
      auto prev = detail::stack_node_base::clear (this->hnode);
      finalize ((node_type *)prev);
    }

  template <class T1, class T2>
  void assign (T1 first, T2 last)
    {
      auto tmp = stack<T> (first, last);
      this->swap (tmp);
      finalize (tmp._Root ());
      tmp._Reset (nullptr);
    }

  void assign (std::initializer_list<T> lst)
    {
      this->assign (lst.begin (), lst.end ());
    }

  ~stack ()
    {
      auto tmp = this->_Root ();
      if (!tmp)
        return;

      _Clean_nodes (tmp);
      this->_Reset (nullptr);
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
