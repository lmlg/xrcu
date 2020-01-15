#ifndef __XRCU_QUEUE_HPP__
#define __XRCU_QUEUE_HPP__   1

#include "xrcu.hpp"
#include "optional.hpp"
#include "xatomic.hpp"
#include <atomic>

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
  size_t cap;
  uintptr_t *ptrs;
  std::atomic<size_t> wr_idx;
  std::atomic<size_t> rd_idx;

  static q_data* make (size_t cnt);

  size_t _Wridx () const
    {
      return (this->wr_idx.load (std::memory_order_relaxed));
    }

  size_t _Rdidx () const
    {
      return (this->rd_idx.load (std::memory_order_relaxed));
    }

  bool push (uintptr_t val)
    {
      while (true)
        {
          size_t curr = this->_Wridx ();
          if (curr >= this->cap)
            return (false);
          else if (xatomic_cas_bool (&this->ptrs[curr], 0, val))
            {
              this->wr_idx.add_fetch (1, std::memory_order_relaxed);
              return (true);
            }

          xatomic_spin_nop ();
        }
    }

  uintptr_t pop (uintptr_t xbit)
    {
      while (true)
        {
          size_t curr = this->_Rdidx ();
          if (curr >= this->_Wridx ())
            return (0);

          uintptr_t rv = this->ptrs[curr];
          if ((rv & xbit) == 0 &&
              this->rd_idx.compare_exchange_weak (curr, curr + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed))
            return (rv);

          xatomic_spin_nop ();
        }
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
  detail::q_data *impl;

  void _Init (size_t size)
    {
      this->impl = detail::q_data::make (size);
    }

  queue ()
    {
      this->_Init (8);
    }

  bool _Rearm (uintptr_t elem, detail::q_data *qdp)
    {
      size_t ix = qdp->_Rdidx ();
      uintptr_t prev = xatomic_or (&qdp->ptrs[ix], 1);
      if (prev & 1)
        {
          while (qdp == this->impl)
            xatomic_spin_nop ();

          return (false);
        }

      detail::q_data_guard g { qdp, ix };
      auto nq = detail::q_data::make (qdp->cap * 2);

      for (ix < qdp->cap; ++ix)
    }
 
  void push (const T& elem)
    {
      cs_guard g;
      auto qdp = this->impl;
      uintptr_t val = this->_Make (elem);

      while (true)
        {
          if (qdp->push (val) || this->_Rearm (val, qdp))
            break;

          while (qdp == this->impl)
            xatomic_spin_nop ();
        }
    }

  optional<T> pop ()
    {
      cs_guard g;
      uintptr_t val = this->impl->pop ();

      if (!val)
        return (optional<T> ());

      optional<T> rv (this->_Get (val));
      this->_Fini (val);
      return (rv);
    }

  size_t size () const
    {
      cs_guard g;
      return (this->impl->size ());
    }
};

} // namespace xrcu

#endif
