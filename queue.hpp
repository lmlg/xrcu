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

struct q_data : public finalizable
{
  uintptr_t *ptrs;
  void *elems;
  size_t cap;
  std::atomic<size_t> wr_idx;
  std::atomic<size_t> rd_idx;
  std::atomic<size_t> invalid;

  q_data (size_t e_size, size_t e_align, size_t cnt);

  size_t _Wridx () const
    {
      return (this->wr_idx.load (std::memory_order_relaxed));
    }

  uintptr_t* _Next (std::atomic<size_t> *idxp, size_t nmax)
    {
      while (true)
        {
          size_t curr = idxp->load (std::memory_order_relaxed);
          if (curr == nmax)
            return (nullptr);
          else if (idxp->compare_exchaneg_weak (curr, curr + 1,
              std::memory_order_acq_rel, std::memory_order_relaxed))
            return (&this->ptrs[curr]);

          xatomic_spin_nop ();
        }
    }

  uintptr_t* push ()
    {
      return (this->_Next (&this->wr_idx, this->cap));
    }

  uintptr_t* pop ()
    {
      while (true)
        {
          uintptr_t *p = this->_Next(&this->rd_idx, this->_Wridx ());
          if (!p || *p)
            return (p);

          xatomic_spin_nop ();
        }
    }

  size_t size () const
    {
      return (this->_Wridx () - this->_Rdidx () -
              this->invalid.load (std::memory_order_relaxed));
    }

  ~q_data ();
};

struct q_ptrs_guard
{
  uintptr_t *px;
  size_t nx;

  q_ptrs_guard (q_data *qdp) : px (qdp->ptrs), nx (qdp->cap)
    {
    }

  ~q_ptrs_guard ()
    {
      for (size_t i = 0; i < this->nx; ++i)
        xatomic_and (&px[i], ~1);
    }
};

} // namespace detail

template <class T>
struct queue
{
  detail::q_data *impl;

  void _Init (size_t size)
    {
      this->impl = new detail::q_data (sizeof (T), alignof (T),
                                       detail::upsize (size));
    }

  queue ()
    {
      this->_Init (8);
    }

  void push (const T& elem)
    {
      cs_guard g;
      auto qdp = this->impl;

      while (true)
        {
          size_t idx = qdp->push_idx ();
          if (idx == (size_t)-1)
            {
              this->_Rearm (qdp);
              continue;
            }

          T *ptr = (T *)qdp->elems + idx;
          try
            {
              new (ptr) T (elem);
            }
          catch (...)
            {
              qdp->invalid.add_fetch (1, std::memory_order_relaxed);
              throw;
            }

          if (xatomic_cas_bool (&qdp->ptrs[idx], 0, (uintptr_t)ptr))
            return;

          /* The queue was being rearmed before we installed the element.
           * Destroy it and try again. */

          detail::destroy<T> (ptr);
          while (qdp == this->impl)
            xatomic_spin_nop ();

          qdp = this->impl;
        }
    }

  optional<T> pop ()
    {
      cs_guard g;
      auto qdp = this->impl;
      size_t idx = qdp->pop_idx ();

      if (idx == (size_t)-1)
        return (optional<T> ());

      return (optional<T> (*((T *)qdp->elems + idx)));
    }

  size_t size () const
    {
      cs_guard g;
      return (this->impl->size ());
    }
};

} // namespace xrcu

#endif
