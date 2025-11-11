/* Declarations for the queue template type.

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

#ifndef __XRCU_QUEUE_HPP__
#define __XRCU_QUEUE_HPP__   1

#include "memory.hpp"
#include "xrcu.hpp"
#include "xatomic.hpp"
#include "utils.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
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

static inline uint32_t upsize (uint32_t x)
{
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  return (x + 1);
}

static inline uint64_t upsize (uint64_t x)
{
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x |= x >> 32;
  return (x + 1);
}

struct alignas (uintptr_t) q_base : public finalizable
{
  uintptr_t *ptrs;
  size_t cap;
  std::atomic<size_t> wr_idx;
  std::atomic<size_t> rd_idx;

  q_base ()
    {
      this->wr_idx.store (0, std::memory_order_relaxed);
      this->rd_idx.store (0, std::memory_order_relaxed);
    }

  size_t _Wridx () const
    {
      return (this->wr_idx.load (std::memory_order_relaxed));
    }

  size_t _Rdidx () const
    {
      return (this->rd_idx.load (std::memory_order_relaxed));
    }

  bool push (uintptr_t val, uintptr_t xbit, uintptr_t empty);
  uintptr_t pop (uintptr_t xbit, uintptr_t dfl);

  uintptr_t front () const
    {
      return (this->ptrs[this->_Rdidx ()]);
    }

  uintptr_t back () const
    {
      size_t idx = this->_Wridx ();
      if (idx == 0)
        return (this->ptrs[this->cap]);

      return (this->ptrs[idx - 1]);
    }

  static void replace (std::atomic<q_base *>&, q_base *,
                       q_base *, uintptr_t);
  static void clear (std::atomic<q_base *>&, q_base *,
                     q_base *, uintptr_t);

  size_t size () const
    {
      return (this->_Wridx () - this->_Rdidx ());
    }
};

} // namespace detail

template <typename T, typename Alloc = std::allocator<T>>
struct queue
{
  typedef detail::wrapped_traits<(sizeof (T) < sizeof (uintptr_t) &&
                                  std::is_integral<T>::value),
                                  T, Alloc> val_traits;

  using Nalloc = typename std::allocator_traits<Alloc>::template
                          rebind_alloc<uintptr_t>;

  struct q_data : public detail::q_base
    {
      q_data ()
        {
        }

      static q_data* make (size_t cnt)
        {
          auto ret = (q_data *)alloc_uptrs<Nalloc> (sizeof (q_data), cnt + 1);
          new (ret) q_data ();
          ret->ptrs = (uintptr_t *)(ret + 1);
          ret->cap = cnt;

          for (uintptr_t *p = ret->ptrs; p != ret->ptrs + cnt + 1; )
            *p++ = val_traits::FREE;

          return (ret);
        }

      void safe_destroy ()
        {
          dealloc_uptrs<Nalloc> (this, this->ptrs + this->cap + 1);
        }
    };

  std::atomic<detail::q_base *> impl;

  typedef T value_type;
  typedef T& reference;
  typedef const T& const_reference;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  void _Init (size_t size)
    {
      this->impl = q_data::make (size);
    }

  q_data* _Data ()
    {
      return ((q_data *)this->impl.load (std::memory_order_relaxed));
    }

  const q_data* _Data () const
    {
      return ((const q_data *)this->impl.load (std::memory_order_relaxed));
    }

  void _Set_data (q_data *qdp)
    {
      this->impl.store (qdp, std::memory_order_relaxed);
    }

  struct iterator : public cs_guard
    {
      const q_data *qdp;
      size_t idx;
      uintptr_t c_val;

      typedef std::forward_iterator_tag iterator_category;

      iterator (const q_data *q, size_t s) : qdp (q), idx (s)
        {
          if (this->qdp)
            this->_Adv ();
        }

      iterator (const iterator& it) :
          qdp (it.qdp), idx (it.idx), c_val (it.c_val)
        {
        }

      iterator (iterator&& it) : qdp (it.qdp), idx (it.idx), c_val (it.c_val)
        {
          it.qdp = nullptr;
        }

      void _Adv ()
        {
          for (; this->idx < this->qdp->cap; ++this->idx)
            {
              this->c_val = this->qdp->ptrs[this->idx] & ~val_traits::XBIT;
              if (this->c_val != val_traits::DELT &&
                  this->c_val != val_traits::FREE)
                return;
            }

          this->qdp = nullptr;
          this->idx = 0;
        }

      T operator* ()
        {
          return (val_traits::get (this->c_val));
        }

      iterator& operator++ ()
        {
          ++this->idx;
          this->_Adv ();
          return (*this);
        }

      iterator operator++ (int)
        {
          iterator rv { *this };
          ++*this;
          return (rv);
        }

      bool operator== (const iterator& it) const
        {
          return (this->qdp == it.qdp && this->idx == it.idx);
        }

      bool operator!= (const iterator& it) const
        {
          return (!(*this == it));
        }
    };

  typedef iterator const_iterator;

  queue ()
    {
      this->_Init (8);
    }

  template <typename T1, typename T2>
  void _Init (T1 n, const T2& val, std::true_type)
    {
      size_t ns = (size_t)n;
      auto qdp = q_data::make (detail::upsize (ns));

      try
        {
          for (size_t i = 0; i < ns; )
            {
              qdp->ptrs[i] = val_traits::make (val);
              qdp->wr_idx.store (++i, std::memory_order_relaxed);
            }
        }
      catch (...)
        {
          _Destroy (qdp);
          throw;
        }

      this->_Set_data (qdp);
    }

  template <typename It>
  void _Init (It first, It last, std::false_type)
    {
      auto qdp = q_data::make (8);
      try
        {
          for (size_t i = 0; first != last; ++first)
            {
              qdp->ptrs[i] = val_traits::make (*first);
              qdp->wr_idx.store (++i, std::memory_order_relaxed);

              if (i == qdp->cap)
                {
                  auto q2 = q_data::make (i * 2);
                  for (size_t j = 0; j < i; ++j)
                    q2->ptrs[j] = qdp->ptrs[j];

                  qdp->safe_destroy ();
                  qdp = q2;
                }
            }
        }
      catch (...)
        {
          _Destroy (qdp);
          throw;
        }

      this->_Set_data (qdp);
    }

  template <typename T1, typename T2>
  queue (T1 first, T2 last)
    {
      this->_Init (first, last, typename std::is_integral<T1>::type ());
    }

  queue (const queue<T, Alloc>& right) : queue (right.begin (), right.end ())
    {
    }

  queue (queue<T, Alloc>&& right)
    {
      this->_Set_data (right._Data ());
      right._Set_data (nullptr);
    }

  queue (std::initializer_list<T> lst) : queue (lst.begin (), lst.end ())
    {
    }

  bool _Rearm (uintptr_t elem, q_data *qdp)
    {
      size_t ix;
      uintptr_t prev;

      while (true)
        {
          ix = qdp->_Rdidx ();
          prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);

          if (prev == val_traits::DELT)
            {  // Another thread deleted this entry - Retry.
              xatomic_spin_nop ();
              continue;
            }
          else if ((prev & val_traits::XBIT) == 0)
            break;

          while (true)
            {
              if (qdp != this->_Data ())
                // The pointer has been installed - Return.
                return (false);
              else if ((qdp->ptrs[ix] & val_traits::XBIT) == 0)
                /*
                 * The thread rearming the queue raised an exception.
                 * Recurse and see if we can pick up the slack.
                 */
                return (this->_Rearm (elem, qdp));

              xatomic_spin_nop ();
            }
        }

      q_data *nq = nullptr;
      try
        {
          nq = q_data::make (qdp->cap * 2);
        }
      catch (...)
        {
          xatomic_and (&qdp->ptrs[ix], ~val_traits::XBIT);
          val_traits::free (elem);
          throw;
        }

      uintptr_t *outp = nq->ptrs;
      for (*outp++ = prev; ++ix < qdp->cap; )
        *outp++ = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);

      *outp++ = elem;
      nq->wr_idx.store (outp - nq->ptrs, std::memory_order_relaxed);
      finalize (qdp);

      this->_Set_data (nq);
      return (true);
    }

  void _Push (uintptr_t val)
    {
      while (true)
        {
          auto qdp = this->_Data ();
          if (qdp->push (val, val_traits::XBIT, val_traits::FREE) ||
              this->_Rearm (val, qdp))
            break;
        }
    }
 
  void push (const T& elem)
    {
      cs_guard g;
      this->_Push (val_traits::make (elem));
    }

  template <typename ...Args>
  void emplace (Args&& ...args)
    {
      cs_guard g;
      this->_Push (val_traits::make (std::forward<Args>(args)...));
    }

  std::optional<T> pop ()
    {
      cs_guard g;

      while (true)
        {
          auto qdp = this->_Data ();
          uintptr_t val = qdp->pop (val_traits::XBIT, val_traits::DELT);

          if (val == val_traits::DELT)
            // Queue is empty.
            return (std::optional<T> ());
          else if (val != val_traits::XBIT)
            {
              val_traits::destroy (val);
              return (std::optional<T> (val_traits::get (val)));
            }

          while (qdp == this->_Data ())
            xatomic_spin_nop ();
        }
    }

  std::optional<T> front () const
    {
      cs_guard g;
      while (true)
        {
          uintptr_t rv = this->_Data()->front () & ~val_traits::XBIT;
          if (rv == val_traits::DELT)
            { // Just popped the item from the queue - Retry.
              xatomic_spin_nop ();
              continue;
            }
          else if (rv == val_traits::FREE)
            return (std::optional<T> ());
          return (std::optional<T> (val_traits::get (rv)));
        }
    }

  std::optional<T> back () const
    {
      cs_guard g;
      while (true)
        {
          uintptr_t rv = this->_Data()->back () & ~val_traits::XBIT;
          if (rv == val_traits::DELT)
            {
              xatomic_spin_nop ();
              continue;
            }
          else if (rv == val_traits::FREE)
            return (std::optional<T> ());
          return (std::optional<T> (val_traits::get (rv)));
        }
    }

  size_t size () const
    {
      cs_guard g;
      return (this->_Data()->size ());
    }

  bool empty () const
    {
      return (this->size () == 0);
    }

  size_t max_size () const
    {
      return (~(size_t)0);
    }

  iterator begin () const
    {
      cs_guard g;
      auto qdp = this->_Data ();
      return (iterator (qdp, qdp->_Rdidx ()));
    }

  iterator end () const
    {
      return (iterator (nullptr, 0));
    }

  const_iterator cbegin () const
    {
      return (this->begin ());
    }

  const_iterator cend () const
    {
      return (this->end ());
    }

  void _Call_cb (q_data *nq, uintptr_t xv,
                 void (*f) (std::atomic<detail::q_base *>&, detail::q_base *,
                            detail::q_base *, uintptr_t))
    {
      while (true)
        {
          auto qdp = this->_Data ();
          size_t ix = qdp->_Rdidx ();
          uintptr_t prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);

          if (prev == val_traits::DELT)
            continue;
          else if ((prev & val_traits::XBIT) == 0)
            {
              if (prev != val_traits::FREE)
                val_traits::destroy (prev);

              while (++ix < qdp->cap)
                {
                  prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);
                  if (prev != val_traits::FREE && prev != val_traits::DELT)
                    val_traits::destroy (prev);
                }

              f (this->impl, qdp, nq, xv);
              return;
            }

          while (true)
            {
              if (qdp != this->_Data () ||
                  (qdp->ptrs[ix] & val_traits::XBIT) == 0)
                break;

              xatomic_spin_nop ();
            }
        }
    }

  void _Assign (q_data *nq)
    {
      this->_Call_cb (nq, 0, detail::q_base::replace);
    }

  template <typename T1, typename T2>
  void assign (T1 first, T2 last)
    {
      cs_guard g;
      auto tmp = queue<T, Alloc> (first, last);
      this->_Assign (tmp._Data ());
      tmp._Set_data (nullptr);
    }

  void assign (std::initializer_list<T> lst)
    {
      this->assign (lst.begin (), lst.end ());
    }

  queue<T, Alloc>& operator= (const queue<T, Alloc>& right)
    {
      if (this == &right)
        return (*this);

      this->assign (right.begin (), right.end ());
      return (*this);
    }

  bool operator== (const queue<T, Alloc>& right) const
    {
      auto x1 = this->cbegin (), x2 = this->cend ();
      auto y1 = right.cbegin (), y2 = right.cend ();

      for (; x1 != x2 && y1 != y2; ++x1, ++y1)
        if (*x1 != *y1)
          return (false);

      return (x1 == x2 && y1 == y2);
    }

  bool operator!= (const queue<T, Alloc>& right) const
    {
      return (!(*this == right));
    }

  bool operator< (const queue<T, Alloc>& right) const
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

  bool operator> (const queue<T, Alloc>& right) const
    {
      return (right < *this);
    }

  bool operator<= (const queue<T, Alloc>& right) const
    {
      return (!(right < *this));
    }

  bool operator>= (const queue<T, Alloc>& right) const
    {
      return (!(*this < right));
    }

  queue<T, Alloc>& operator= (queue<T, Alloc>&& right)
    {
      auto prev = this->impl.exchange (right._Data (),
                                       std::memory_order_acq_rel);
      finalize (prev);
      right._Set_data (nullptr);
      return (*this);
    }

  static void _Destroy (q_data *qdp)
    {
      for (size_t i = qdp->_Rdidx (); i < qdp->cap; ++i)
        {
          uintptr_t val = qdp->ptrs[i] & ~val_traits::XBIT;
          if (val != val_traits::FREE && val != val_traits::DELT)
            val_traits::free (val);
        }

      qdp->safe_destroy ();
    }

  void clear ()
    {
      cs_guard g;
      this->_Call_cb (nullptr, val_traits::FREE, detail::q_base::clear);
    }

  size_t _Lock ()
    {
      while (true)
        {
          auto qdp = this->_Data ();
          size_t ix = qdp->_Rdidx ();
          uintptr_t prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);

          if ((prev & val_traits::XBIT) == 0)
            return (qdp->wr_idx.exchange (qdp->cap,
                                          std::memory_order_acq_rel));

          while (qdp == this->_Data () &&
              (qdp->ptrs[ix] & val_traits::XBIT) != 0)
            xatomic_spin_nop ();
        }
    }

  void swap (queue<T, Alloc>& right)
    {
      if (this == &right)
        return;

      size_t s1 = this->_Lock ();
      size_t s2 = right._Lock ();

      auto tmp = this->_Data ();
      this->_Set_data (right._Data ());
      right._Set_data (tmp);

      auto d1 = this->_Data();
      auto d2 = right._Data();

      d1->wr_idx.store (s2, std::memory_order_relaxed);
      d2->wr_idx.store (s1, std::memory_order_relaxed);

      d1->ptrs[d1->_Rdidx ()] &= ~val_traits::XBIT;
      d2->ptrs[d2->_Rdidx ()] &= ~val_traits::XBIT;
    }

  ~queue ()
    {
      auto qdp = this->_Data ();
      if (!qdp)
        return;

      _Destroy (qdp);
      this->_Set_data (nullptr);
    }
};

} // namespace xrcu

namespace std
{

template <typename T, typename Alloc>
void swap (xrcu::queue<T, Alloc>& left, xrcu::queue<T, Alloc>& right)
{
  left.swap (right);
}

} // namespace std

#endif
