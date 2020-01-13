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
};

template <class T>
struct stack_node : public stack_node_base, finalizable
{
  T value;

  stack_node (const T& v) : value (v) {}
  stack_node (T&& v) : value (std::move (v)) {}

  template <class ...Args>
  stack_node (Args&&... args) : value (std::forward<Args>(args)...) {}
};

struct stack_base
{
  std::atomic<stack_node_base *> rnode;
  std::atomic<size_t> size;

  void reset (stack_node_base *nodep, size_t sz)
    {
      this->rnode.store (nodep, std::memory_order_relaxed);
      this->size.store (sz, std::memory_order_relaxed);
    }

  stack_node_base* root () const;

  void push_node (stack_node_base *nodep);
  void push_nodes (stack_node_base *nodep,
    stack_node_base **outp, size_t cnt);
  stack_node_base* pop_node ();

  bool empty () const
    {
      return (this->root () == nullptr);
    }

  void swap (stack_base& right);
};

struct stack_iter_base : public cs_guard
{
  stack_node_base *runp;
  typedef std::forward_iterator_tag iterator_category;
  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  stack_iter_base (stack_node_base *rp) : runp (rp) {}

  stack_iter_base& operator++ ()
    {
      this->runp = this->runp->next;
      return (*this);
    }

  stack_iter_base operator++ (int)
    {
      stack_iter_base tmp (this->runp);
      ++*this;
      return (tmp);
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
  struct _Stkbase : public finalizable
    {
      detail::stack_base sb;

      _Stkbase ()
        {
          this->sb.reset (nullptr, 0);
        }

      static void
      clean_nodes (detail::stack_node_base *runp)
        {
          while (runp != nullptr)
            {
              auto tmp = runp->next;
              delete (detail::stack_node<T> *)runp;
              runp = tmp;
            }
        }

      ~_Stkbase ()
        {
          clean_nodes (this->sb.root ());
          this->sb.reset (nullptr, 0);
        }
    };

  std::atomic<_Stkbase *> basep;

  typedef detail::stack_node<T> node_type;
  typedef T value_type;
  typedef T& reference;
  typedef const T& const_reference;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  _Stkbase* _Base ()
    {
      return (this->basep.load (std::memory_order_relaxed));
    }

  const _Stkbase* _Base () const
    {
      return (this->basep.load (std::memory_order_relaxed));
    }

  template <class T1>
  void _Init (T1 first, T1 last, std::false_type)
    {
      size_t len = 0;
      detail::stack_node_base *runp = nullptr, **outp = &runp;
      try
        {
          for (; first != last; ++first, ++len)
            {
              *outp = new node_type (*first);
              outp = &(*outp)->next;
            }
        }
      catch (...)
        {
          _Stkbase::clean_nodes (runp);
          throw;
        }

      this->_Base()->sb.reset (runp, len);
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
          _Stkbase::clean_nodes (runp);
          throw;
        }

      this->_Base()->sb.reset (runp, n);
    }

  void _Init_base ()
    {
      this->basep.store (new _Stkbase (), std::memory_order_relaxed);
    }

  stack ()
    {
      this->_Init_base ();
    }

  template <class T1, class T2>
  stack (T1 first, T2 last)
    {
      this->_Init_base ();
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
      this->basep.store (right.basep.load (std::memory_order_relaxed),
                         std::memory_order_relaxed);
      right.basep.store (nullptr, std::memory_order_relaxed);
    }

  void push (const T& value)
    {
      this->_Base()->sb.push_node (new node_type (value));
    }

  void push (T&& value)
    {
      this->_Base()->sb.push_node (new node_type (std::move (value)));
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
          size_t cnt = 1;

          for (++first; first != last; ++first, ++cnt)
            {
              node_type *tmp = new node_type (*first);
              *outp = tmp, outp = &tmp->next;
            }

          this->_Base()->sb.push_nodes (np, outp, cnt);
        }
      catch (...)
        {
          _Stkbase::clean_nodes (np);
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
          T1 cnt = n;

          for (; n != 0; --n)
            {
              node_type *tmp = new node_type (value);
              *outp = tmp, outp = &tmp->next;
            }

          this->_Base()->sb.push_nodes (np, outp, cnt);
        }
      catch (...)
        {
          _Stkbase::clean_nodes (np);
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
      this->_Base()->sb.push_node (new node_type (std::forward<Args>(args)...));
    }

  optional<T> pop ()
    {
      cs_guard g;
      auto node = (node_type *)this->_Base()->sb.pop_node ();

      if (!node)
        return (optional<T> ());

      optional<T> ret { node->value };
      finalize (node);
      return (ret);
    }

  optional<T> top ()
    {
      cs_guard g;
      auto node = (node_type *)this->_Base()->sb.root ();
      return (node ? optional<T> { node->value } : optional<T> ());
    }

  struct iterator : public detail::stack_iter_base
    {
      typedef T value_type;
      typedef T& reference;
      typedef T* pointer;

      iterator (detail::stack_node_base *rp = nullptr) :
        detail::stack_iter_base (rp) {}

      T& operator* ()
        {
          return (((node_type *)this->runp)->value);
        }

      T* operator-> ()
        {
          return (&**this);
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

      const T* operator-> () const
        {
          return (&**this);
        }
    };

  iterator begin ()
    {
      return (iterator (this->_Base()->sb.root ()));
    }

  iterator end ()
    {
      return (iterator ());
    }

  const_iterator cbegin () const
    {
      return (const_iterator (this->_Base()->sb.root ()));
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
      return (this->_Base()->sb.size.load (std::memory_order_relaxed));
    }

  size_t max_size () const
    {
      return (~(size_t)0);
    }

  bool empty () const
    {
      return (this->_Base()->sb.empty ());
    }

  void swap (stack<T>& right)
    {
      cs_guard g;

      if (this != &right)
        this->_Base()->sb.swap (right._Base()->sb);
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
      auto prev = this->basep.exchange (right._Base (),
                                        std::memory_order_acq_rel);
      finalize (prev);
      right.basep.store (nullptr, std::memory_order_relaxed);
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
      auto prev = this->basep.exchange (new _Stkbase (),
                                        std::memory_order_acq_rel);
      finalize (prev);
    }

  template <class T1, class T2>
  void assign (T1 first, T2 last)
    {
      auto tmp = stack<T> (first, last);
      auto prev = this->basep.exchange (tmp._Base (),
                                        std::memory_order_acq_rel);
      finalize (prev);
      tmp.basep.store (nullptr, std::memory_order_relaxed);
    }

  void assign (std::initializer_list<T> lst)
    {
      this->assign (lst.begin (), lst.end ());
    }

  ~stack ()
    {
      delete this->_Base ();
      this->basep.store (nullptr, std::memory_order_relaxed);
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
