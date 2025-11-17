/* Declarations for the stack template type.

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

#ifndef __XRCU_STACK_HPP__
#define __XRCU_STACK_HPP__   1

#include "utils.hpp"
#include "xrcu.hpp"

#include <atomic>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
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
  stack_node_base *next = nullptr;
  typedef std::atomic<stack_node_base *> ptr_type;

  static stack_node_base* root (const ptr_type& head);

  static void push (ptr_type& head, stack_node_base *nodep);

  static void push (ptr_type& head, stack_node_base *nodep,
                    stack_node_base **outp);

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

template <typename T, typename Alloc = std::allocator<T>>
struct stack
{
  struct _Stknode : public detail::stack_node_base, finalizable
    {
      T value;

      _Stknode (const T& v) : value (v)
        {
        }

      _Stknode (T&& v) : value (std::move (v))
        {
        }

      template <typename ...Args>
      _Stknode (Args&&... args) : value (std::forward<Args>(args)...)
        {
        }

      static _Stknode* alloc (const T& val)
        {
          auto ret = Nalloc().allocate (1);
          try
            {
              new (ret) _Stknode (val);
              return (ret);
            }
          catch (...)
            {
              Nalloc().deallocate (ret, 1);
              throw;
            }
        }

      template <typename ...Args>
      static _Stknode* move (Args... args)
        {
          auto ret = Nalloc().allocate (1);
          new (ret) _Stknode (std::forward<Args>(args)...);
          return (ret);
        }

      void safe_destroy ()
        {
          stack<T, Alloc>::_Clean_nodes (this);
        }
    };

  std::atomic<detail::stack_node_base *> hnode;
  using Nalloc = typename std::allocator_traits<Alloc>::template
                 rebind_alloc<_Stknode>;

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

  static void _Destroy (_Stknode *node)
    {
      node->value.~T ();
      Nalloc().deallocate (node, 1);
    }

  static void _Clean_nodes (detail::stack_node_base *runp)
    {
      while (runp)
        {
          auto tmp = runp->next;
          stack<T, Alloc>::_Destroy ((_Stknode *)runp);
          runp = tmp;
        }
    }

  template <typename T1>
  void _Init (T1 first, T1 last, std::false_type)
    {
      detail::stack_node_base *runp = nullptr, **outp = &runp;
      try
        {
          for (; first != last; ++first)
            {
              *outp = _Stknode::alloc (*first);
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

  template <typename T1, typename T2>
  void _Init (T1 n, const T2& value, std::true_type)
    {
      detail::stack_node_base *runp = nullptr, **outp = &runp;

      try
        {
          for (T1 x = 0; x != n; ++x)
            {
              *outp = _Stknode::alloc (value);
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

  template <typename T1, typename T2>
  stack (T1 first, T2 last)
    {
      this->_Reset (nullptr);
      this->_Init (first, last, typename std::is_integral<T1>::type ());
    }

  stack (std::initializer_list<T> lst) : stack (lst.begin (), lst.end ())
    {
    }

  stack (const stack<T, Alloc>& right) : stack (right.begin (), right.end ())
    {
    }

  stack (stack<T, Alloc>&& right)
    {
      this->_Reset (right._Root ());
      right._Reset (nullptr);
    }

  void push (const T& value)
    {
      cs_guard g;
      detail::stack_node_base::push (this->hnode, _Stknode::alloc (value));
    }

  void push (T&& value)
    {
      cs_guard g;
      detail::stack_node_base::push (this->hnode,
                                     _Stknode::move (std::move (value)));
    }

  template <typename Iter>
  void _Push (Iter first, Iter last, std::false_type)
    {
      if (first == last)
        return;

      node_type *np = nullptr;

      try
        {
          np = _Stknode::alloc (*first);
          auto **outp = &np->next;

          for (++first; first != last; ++first)
            {
              node_type *tmp = _Stknode::alloc (*first);
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

  template <typename T1, typename T2>
  void _Push (T1 n, const T2& value, std::true_type)
    {
      if (n == 0)
        return;

      node_type *np = nullptr;

      try
        {
          np = _Stknode::alloc (value);
          auto **outp = &np->next;

          for (; n != 0; --n)
            {
              node_type *tmp = _Stknode::alloc (value);
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

  template <typename T1, typename T2>
  void push (T1 first, T2 last)
    {
      this->_Push (first, last, typename std::is_integral<T1>::type ());
    }

  template <typename...Args>
  void emplace (Args&& ...args)
    {
      auto np = _Stknode::move (std::forward<Args>(args)...);
      detail::stack_node_base::push (this->hnode, np);
    }

  std::optional<T> pop ()
    {
      cs_guard g;
      auto node = detail::stack_node_base::pop (this->hnode);

      if (!node)
        return (std::optional<T> ());

      std::optional<T> ret { ((node_type *)node)->value };
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

  std::optional<T> top ()
    {
      cs_guard g;
      auto node = this->_Root ();
      return (node ? std::optional<T> { node->value } : std::optional<T> ());
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

  void swap (stack<T, Alloc>& right)
    {
      cs_guard g;

      if (this != &right)
        detail::stack_node_base::swap (this->hnode, right.hnode);
    }

  stack<T, Alloc>& operator= (const stack<T, Alloc>& right)
    {
      cs_guard g;

      if (this != &right)
        this->assign (right.begin (), right.end ());

      return (*this);
    }

  stack<T, Alloc>& operator= (stack<T, Alloc>&& right)
    {
      this->swap (right);
      finalize (right._Root ());
      right._Reset (nullptr);
      return (*this);
    }

  template <typename T2, typename A2>
  bool operator== (const stack<T2, A2>& right) const
    {
      return (detail::sequence_eq (this->cbegin (), this->cend (),
                                   right.cbegin (), right.cend ()));
    }

  template <typename T2, typename A2>
  bool operator!= (const stack<T2, A2>& right) const
    {
      return (!(*this == right));
    }

  template <typename T2, typename A2>
  bool operator< (const stack<T2, A2>& right) const
    {
      return (detail::sequence_lt (this->cbegin (), this->cend (),
                                   right.cbegin (), right.cend ()));
    }

  template <typename T2, typename A2>
  bool operator> (const stack<T2, A2>& right) const
    {
      return (right < *this);
    }

  template <typename T2, typename A2>
  bool operator<= (const stack<T2, A2>& right) const
    {
      return (!(right < *this));
    }

  template <typename T2, typename A2>
  bool operator>= (const stack<T2, A2>& right) const
    {
      return (!(*this < right));
    }

  void clear ()
    {
      auto prev = detail::stack_node_base::clear (this->hnode);
      finalize ((node_type *)prev);
    }

  template <typename T1, typename T2>
  void assign (T1 first, T2 last)
    {
      auto tmp = stack<T, Alloc> (first, last);
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

template <typename T, typename Alloc>
void swap (xrcu::stack<T, Alloc>& left, xrcu::stack<T, Alloc>& right)
{
  return (left.swap (right));
}

} // namespace std

#endif
