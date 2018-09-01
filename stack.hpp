#ifndef __XRCU_STACK_HPP__
#define __XRCU_STACK_HPP__   1

#include "xrcu.hpp"
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <type_traits>
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

  void safe_destroy ()
    {
      delete this;
    }

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

  void destroy (void (*fct) (stack_node_base *))
    {
      for (auto runp = this->root (); runp != nullptr; )
        {
          auto next = runp->next;
          fct (runp), runp = next;
        }

      this->reset (nullptr, 0);
    }

  stack_node_base* root () const
    {
      return (this->rnode.load (std::memory_order_relaxed));
    }

  void push_node (stack_node_base *nodep);
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
  struct _Stkbase : public finalizable
    {
      detail::stack_base sb;

      _Stkbase ()
        {
          this->sb.reset (nullptr, 0);
        }

      static void
      fini (detail::stack_node_base *nodep)
        {
          delete (detail::stack_node<T> *)nodep;
        }

      void safe_destroy ()
        {
          this->sb.destroy (fini);
        }

      ~_Stkbase ()
        {
          this->safe_destroy ();
        }
    };

  std::atomic<_Stkbase *> basep;
  typedef detail::stack_node<T> node_type;

  _Stkbase* _Base ()
    {
      return (this->basep.load (std::memory_order_relaxed));
    }

  const _Stkbase* _Base () const
    {
      return (this->basep.load (std::memory_order_relaxed));
    }

  static void
  _Clean_nodes (detail::stack_node_base *runp)
    {
      while (runp != nullptr)
        {
          auto tmp = runp->next;
          _Stkbase::fini (runp);
          runp = tmp;
        }
    }

  template <class Iter>
  void _Init (Iter first, Iter last, std::false_type)
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
          _Clean_nodes (runp);
          throw;
        }

      this->_Base()->sb.reset (runp, len);
    }

  template <class Iter>
  void _Init (Iter n, Iter value, std::true_type)
    {
      detail::stack_node_base *runp = nullptr, **outp = &runp;

      try
        {
          for (Iter x = 0; x != n; ++x)
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

  template <class Iter>
  stack (Iter first, Iter last)
    {
      this->_Init_base ();
      this->_Init (first, last, typename std::is_integral<Iter>::type ());
    }

  stack (std::initializer_list<T> lst) : stack (lst.begin (), lst.end ())
    {
    }

  stack (const stack<T>& right) : stack (right.begin (), right.end ())
    {
    }

  stack (stack<T>&& right) : stack (right.begin (), right.end ())
    {
    }

  void push (const T& value)
    {
      this->_Base()->sb.push_node (new node_type (value));
    }

  template <class ...Args>
  void emplace (Args&& ...args)
    {
      this->_Base()->sb.push_node (new node_type (args...));
    }

  T pop ()
    {
      cs_guard g;
      auto node = (node_type *)this->_Base()->sb.pop_node ();

      if (!node)
        throw std::runtime_error ("stack<T>::pop: stack is empty");

      T ret = node->value;
      finalize (node);
      return (ret);
    }

  T top ()
    {
      cs_guard g;
      auto node = (node_type *)this->_Base()->sb.root ();

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

  bool empty () const
    {
      return (this->_Base()->sb.root () == nullptr);
    }

  void swap (stack<T>& right)
    {
      if (this != &right)
        this->_Base()->sb.swap (right._Base()->sb);
    }

  stack<T>& operator= (const stack<T>& right)
    {
      if (this == &right)
        return (*this);

      stack<T> tmp (right);
      auto prev = this->basep.exchange (tmp._Base (),
                                        std::memory_order_acq_rel);

      tmp.basep.store (nullptr, std::memory_order_relaxed);
      if (prev)
        finalize (prev);

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
