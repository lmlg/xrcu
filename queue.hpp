#ifndef __XRCU_QUEUE_HPP__
#define __XRCU_QUEUE_HPP__   1

#include "xrcu.hpp"
#include "optional.hpp"
#include "xatomic.hpp"
#include "utils.hpp"
#include <cstddef>
#include <atomic>
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

static constexpr size_t min_size (size_t sz)
{
  return (sz < 2 ? 2 : sz);
}

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

struct alignas (uintptr_t) q_data : public finalizable
{
  uintptr_t *ptrs;
  size_t cap;
  std::atomic<size_t> wr_idx;
  std::atomic<size_t> rd_idx;

  static q_data* make (size_t cnt, uintptr_t empty);

  size_t _Wridx () const
    {
      return (this->wr_idx.load (std::memory_order_relaxed));
    }

  size_t _Rdidx () const
    {
      return (this->rd_idx.load (std::memory_order_relaxed));
    }

  bool push (uintptr_t val, uintptr_t xbit, uintptr_t empty)
    {
      while (true)
        {
          size_t curr = this->_Wridx ();
          if (curr >= this->cap)
            return (false);

          uintptr_t xv = this->ptrs[curr];
          if ((xv & xbit) != 0)
            return (false);
          else if (xv == empty &&
              xatomic_cas_bool (&this->ptrs[curr], xv, val))
            {
              this->wr_idx.fetch_add (1, std::memory_order_relaxed);
              return (true);
            }

          xatomic_spin_nop ();
        }
    }

  uintptr_t pop (uintptr_t xbit, uintptr_t dfl)
    {
      while (true)
        {
          size_t curr = this->_Rdidx ();
          if (curr >= this->_Wridx ())
            return (dfl);

          uintptr_t rv = this->ptrs[curr];
          if ((rv & xbit) != 0 || rv == dfl)
            return (xbit);
          else if (xatomic_cas_bool (&this->ptrs[curr], rv, dfl))
            {
              this->rd_idx.fetch_add (1, std::memory_order_relaxed);
              return (rv);
            }

          xatomic_spin_nop ();
        }
    }

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

  size_t size () const
    {
      return (this->_Wridx () - this->_Rdidx ());
    }

  void safe_destroy ();
};

} // namespace detail

template <class T>
struct queue
{
  typedef detail::wrapped_traits<(sizeof (T) < sizeof (uintptr_t) &&
      std::is_integral<T>::value) || (std::is_pointer<T>::value &&
      alignof (T) >= 8), T> val_traits;

  std::atomic<detail::q_data *> impl;

  void _Init (size_t size)
    {
      this->impl = detail::q_data::make (size, val_traits::FREE);
    }

  detail::q_data* _Data ()
    {
      return (this->impl.load (std::memory_order_relaxed));
    }

  const detail::q_data* _Data () const
    {
      return (this->impl.load (std::memory_order_relaxed));
    }

  void _Set_data (detail::q_data *qdp)
    {
      this->impl.store (qdp, std::memory_order_relaxed);
    }

  struct iterator : public cs_guard
    {
      detail::q_data *qdp;
      size_t idx;
      uintptr_t c_val;

      typedef std::forward_iterator_tag iterator_category;

      iterator (detail::q_data *q, size_t s) : qdp (q), idx (s)
        {
          if (this->qdp)
            this->_Adv ();
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
          this->_Adv ();
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

  template <class T1, class T2>
  void _Init (T1 n, const T2& val, std::true_type)
    {
      auto qdp = detail::q_data::make (detail::upsize (n), val_traits::FREE);

      try
        {
          for (size_t i = 0; i < n; )
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

  template <class It>
  void _Init (It first, It last, std::false_type)
    {
      auto qdp = detail::q_data::make (8, val_traits::FREE);
      try
        {
          for (size_t i = 0; first != last; ++first)
            {
              qdp->ptrs[i] = val_traits::make (*first);
              qdp->wr_idx.store (++i, std::memory_order_relaxed);

              if (i == qdp->cap)
                {
                  auto q2 = detail::q_data::make (i * 2, val_traits::FREE);
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

  template <class T1, class T2>
  queue (T1 first, T2 last)
    {
      this->_Init (first, last, typename std::is_integral<T1>::type ());
    }

  queue (const queue<T>& right) : queue (right.begin (), right.end ())
    {
    }

  queue (queue<T>&& right)
    {
      this->_Set_data (right._Data ());
      right._Set_data (nullptr);
    }

  queue (std::initializer_list<T> lst) : queue (lst.begin (), lst.end ())
    {
    }

  bool _Rearm (uintptr_t elem, detail::q_data *qdp)
    {
      size_t ix;
      uintptr_t prev;

      while (true)
        {
          ix = qdp->_Rdidx ();
          prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);

          if (prev == val_traits::DELT)
            // Another thread deleted this entry - Retry.
            continue;
          else if ((prev & val_traits::XBIT) == 0)
            break;

          while (true)
            {
              if (qdp != this->_Data ())
                // Impl pointer has been installed - Return.
                return (false);
              else if ((qdp->ptrs[ix] & val_traits::XBIT) == 0)
                /* The thread rearming the queue raised an exception.
                 * Recurse and see if we can pick up the slack. */
                return (this->_Rearm (elem, qdp));

              xatomic_spin_nop ();
            }
        }

      detail::q_data *nq = nullptr;
      try
        {
          nq = detail::q_data::make (qdp->cap * 2, val_traits::FREE);
        }
      catch (...)
        {
          xatomic_and (&qdp->ptrs[ix], ~val_traits::XBIT);
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
 
  void push (const T& elem)
    {
      cs_guard g;
      uintptr_t val = val_traits::make (elem);

      while (true)
        {
          auto qdp = this->_Data ();
          if (qdp->push (val, val_traits::XBIT, val_traits::FREE) ||
              this->_Rearm (val, qdp))
            break;
        }
    }

  optional<T> pop ()
    {
      cs_guard g;

      while (true)
        {
          auto qdp = this->_Data ();
          uintptr_t val = qdp->pop (val_traits::XBIT, val_traits::DELT);

          if (val == val_traits::DELT)
            // Queue is empty.
            return (optional<T> ());
          else if (val != val_traits::XBIT)
            {
              val_traits::destroy (val);
              optional<T> rv (val_traits::get (val));
              return (rv);
            }

          while (qdp == this->_Data ())
            xatomic_spin_nop ();
        }
    }

  optional<T> front () const
    {
      cs_guard g;
      uintptr_t rv = this->_Data()->front () & ~val_traits::XBIT;

      if (rv == val_traits::FREE)
        return (optional<T> ());

      return (optional<T> (val_traits::get (rv)));
    }

  optional<T> back () const
    {
      cs_guard g;
      uintptr_t rv = this->_Data()->back () & ~val_traits::XBIT;

      if (rv == val_traits::FREE)
        return (optional<T> ());

      return (optional<T> (val_traits::get (rv)));
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
      return (iterator (this->_Data (), this->_Data()->_Rdidx ()));
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

  void _Assign (detail::q_data *nq)
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

              for (; ix < qdp->cap; ++ix)
                {
                  prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);
                  if (prev != val_traits::FREE)
                    val_traits::destroy (prev);
                }

              finalize (qdp);
              this->_Set_data (nq);
              return;
            }

          while (true)
            {
              if (qdp != this->_Data ())
                break;
              else if ((qdp->ptrs[ix] & val_traits::XBIT) == 0)
                break;

              xatomic_spin_nop ();
            }
        }
    }

  void clear ()
    {
      cs_guard g;
      this->_Assign (detail::q_data::make (8, val_traits::FREE));
    }

  template <class T1, class T2>
  void assign (T1 first, T2 last)
    {
      cs_guard g;
      auto tmp = queue<T> (first, last);
      this->_Assign (tmp._Data ());
      tmp._Set_data (nullptr);
    }

  void assign (std::initializer_list<T> lst)
    {
      this->assign (lst.begin (), lst.end ());
    }

  queue<T>& operator= (const queue<T>& right)
    {
      if (this == &right)
        return (*this);

      this->assign (right.begin (), right.end ());
      return (*this);
    }

  bool operator== (const queue<T>& right) const
    {
      auto x1 = this->cbegin (), x2 = this->cend ();
      auto y1 = right.cbegin (), y2 = right.cend ();

      for (; x1 != x2 && y1 != y2; ++x1, ++y1)
        if (*x1 != *x2)
          return (false);

      return (x1 == x2 && y1 == y2);
    }

  bool operator!= (const queue<T>& right) const
    {
      return (!(*this == right));
    }

  bool operator< (const queue<T>& right) const
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

  bool operator> (const queue<T>& right) const
    {
      return (right < *this);
    }

  bool operator<= (const queue<T>& right) const
    {
      return (!(right < *this));
    }

  bool operator>= (const queue<T>& right) const
    {
      return (!(*this < right));
    }

  queue<T>& operator= (queue<T>&& right)
    {
      auto prev = this->impl.exchange (right._Data (),
                                       std::memory_order_acq_rel);
      finalize (prev);
      right._Set_data (nullptr);
      return (*this);
    }

  static void _Destroy (detail::q_data *qdp)
    {
      for (size_t i = qdp->_Rdidx (); i < qdp->cap; ++i)
        {
          uintptr_t val = qdp->ptrs[i] & ~val_traits::XBIT;
          if (val != val_traits::FREE && val != val_traits::DELT)
            val_traits::free (val);
        }

      qdp->safe_destroy ();
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

#endif
