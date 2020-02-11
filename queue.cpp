#include "queue.hpp"
#include <new>

namespace xrcu
{

namespace detail
{

q_data* q_data::make (size_t cnt, uintptr_t empty)
{
  size_t ns = cnt + 1;
  q_data *ret = (q_data *)::operator new (sizeof (*ret) +
                                          ns * sizeof (uintptr_t));

  new (ret) q_data ();
  ret->ptrs = (uintptr_t *)(ret + 1);
  ret->cap = cnt;

  for (uintptr_t *p = ret->ptrs; p != ret->ptrs + ns; )
    *p++ = empty;

  ret->wr_idx.store (0, std::memory_order_relaxed);
  ret->rd_idx.store (0, std::memory_order_relaxed);
  return (ret);
}

void q_data::safe_destroy ()
{
  ::operator delete (this);
}

} // namespace detail

} // namespace xrcu
