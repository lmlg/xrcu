#ifndef __XRCU_QUEUE_HPP__
#define __XRCU_QUEUE_HPP__   1

#include "xrcu.hpp"
#include "optional.hpp"
#include "xatomic.hpp"
#include "utils.hpp"
#include <atomic>
#include <type_traits>

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

static inline uint32_t roundup (uint32_t x)
{
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  return (x + 1);
}

static inline uint64_t roundup (uint64_t x)
{
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x |= x >> 32;
  return (x + 1);
}

static inline void
destroy (T *ptr)
{
  ptr->~T ();
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
              this->wr_idx.add_fetch (1, std::memory_order_relaxed);
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
          if ((rv & xbit) != 0)
            return (xbit);
          else if (xatomic_cas_bool (&this->ptrs[curr], rv, dfl))
            {
              this->rd_idx.add_fetch (1, std::memory_order_relaxed);
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

struct q_data_guard
{
  q_data *qdp;
  size_t statrt;
  uintptr_t bit;

  q_data_guard (q_data *q, size_t s, uintptr_t b) : qdp (q), start (s), bit (b)
    {
    }

  void disable ()
    {
      this->qdp = nullptr;
    }

  ~q_data_guard ()
    {
      if (!this->qdp)
        return;

      for (size_t i = this->start; i < this->qdp->cap + 1; ++i)
        xatomic_and (this->qdp->ptrs[i], ~this->bit);
    }
};

} // namespace detail

template <class T>
struct queue
{
  detail::q_data *impl;

  typedef detail::wrapped_traits<(sizeof (T) < sizeof (uintptr_t) &&
      std::is_integral<T>::value) || (std::is_pointer<T>::value &&
      alignof (T) >= 8), T> val_traits;

  void _Init (size_t size)
    {
      this->impl = detail::q_data::make (size, val_traits::FREE);
    }

  struct iterator : public cs_guard
    {
      detail::q_data *qdp;
      size_t idx;
      uintptr_t c_val;

      typedef std::forward_iterator_tag iterator_category;

      iterator (detail::q_data *q, size_t s) : qdp (q), idx (s)
        {
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

  queue ()
    {
      this->_Init (8);
    }

  bool _Rearm (uintptr_t elem, detail::q_data *qdp)
    {
      size_t ix = qdp->_Rdidx ();
      uintptr_t prev = xatomic_or (&qdp->ptrs[ix], val_traits::XBIT);
      if (prev & val_traits::XBIT)
        {
          while (qdp == this->impl)
            xatomic_spin_nop ();

          return (false);
        }

      detail::q_data_guard g { qdp, ix };
      auto nq = detail::q_data::make (qdp->cap * 2);
      uintptr_t *outp = nq->ptrs;

      for (*outp++ = prev; ix < qdp->cap + 1; ++ix)
        *outp++ = xatomic_or (&this->qdp[ix], val_traits::XBIT);

      *outp++ = elem;
      nq->wr_idx.store (outp - nq->ptrs - 1, std::memory_order_relaxed);
      finalize (qdp);
      this->qdp = nq;
      std::atomic_thread_fence (std::memory_order_release);
      return (true);
    }
 
  void push (const T& elem)
    {
      cs_guard g;
      auto qdp = this->impl;
      uintptr_t val = val_traits::make (elem);

      while (true)
        {
          if (qdp->push (val, val_traits::XBIT) || this->_Rearm (val, qdp))
            break;

          while (qdp == this->impl)
            xatomic_spin_nop ();
        }
    }

  optional<T> pop ()
    {
      cs_guard g;
      auto qdp = this->impl;

      while (true)
        {
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

          while (qdp == this->impl)
            xatomic_spin_nop ();

          qdp = this->impl;
        }
    }

  optional<T> front () const
    {
      cs_guard g;
      uintptr_t rv = this->impl->front () & ~val_traits::XBIT;

      if (rv == val_traits::FREE)
        return (optional<T> ());

      return (optional<T> (val_traits::get (rv)));
    }

  optional<T> back () const
    {
      cs_guard g;
      uintptr_t rv = this->impl->back () & ~val_traits::XBIT;

      if (rv == val_traits::FREE)
        return (optional<T> ());

      return (optional<T> (val_traits::get (rv)));
    }

  size_t size () const
    {
      cs_guard g;
      return (this->impl->size ());
    }
};

} // namespace xrcu

#endif
