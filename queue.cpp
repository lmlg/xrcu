#include "queue.hpp"
#include <cstring>
#include <new>

namespace xrcu
{

namespace detail
{

q_data* q_data::make (size_t cnt)
{
  q_data *ret = (q_data *)::operator new (sizeof (*ret) +
                                          cnt * sizeof (uintptr_t));
  ret->ptrs = (uintptr_t *)(ret + 1);
  memset (ret->ptrs, 0, cnt * sizeof (uintptr_t));
  ret->wr_idx.store (0, std::memory_order_relaxed);
  ret->rd_idx.store (0, std::memory_order_relaxed);
}

void q_data::safe_destroy ()
{
  ::operator delete (this);
}

} // namespace detail

} // namespace xrcu
