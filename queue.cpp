#include "queue.hpp"
#include <cstring>
#include <new>

namespace xrcu
{

namespace detail
{

q_data::q_data (size_t e_size, size_t e_align, size_t cnt)
{
  e_size = min_size (e_size);
  size_t total = cnt * e_size + e_align + cnt * sizeof (uintptr_t);
  void *p = ::operator new (total);

  this->ptrs = (uintptr_t *)memset (p, 0, cnt * sizeof (uintptr_t));
  this->elems = (char *)((((uintptr_t)this->ptrs + cnt) +
                 e_align) & ~(e_align - 1));

  this->wr_idx.store (0, std::memory_order_relaxed);
  this->rd_idx.store (0, std::memory_order_relaxed);
  this->invalid.store (0, std::memory_order_relaxed);
}

q_data::~q_data ()
{
  ::operator delete (this->ptrs);
}

} // namespace detail

} // namespace xrcu
