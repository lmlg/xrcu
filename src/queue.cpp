/* Definitions for the queue template type.

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

#include "xrcu/queue.hpp"

namespace xrcu
{

namespace detail
{

bool q_base::push (uintptr_t val, uintptr_t xbit, uintptr_t empty)
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
          this->wr_idx.fetch_add (1, std::memory_order_acq_rel);
          return (true);
        }

      xatomic_spin_nop ();
    }
}

uintptr_t q_base::pop (uintptr_t xbit, uintptr_t dfl)
{
  while (true)
    {
      size_t curr = this->_Rdidx ();
      if (curr >= this->_Wridx ())
        return (dfl);

      uintptr_t rv = this->ptrs[curr];
      if (rv & xbit)
        return (xbit);
      else if (rv != dfl && xatomic_cas_bool (&this->ptrs[curr], rv, dfl))
        {
          this->rd_idx.fetch_add (1, std::memory_order_relaxed);
          return (rv);
        }

      xatomic_spin_nop ();
    }
}

void q_base::replace (std::atomic<q_base *>& ptr, q_base *old,
                      q_base *nq, uintptr_t)
{
  finalize (old);
  ptr.store (nq, std::memory_order_relaxed);
}

void q_base::clear (std::atomic<q_base *>&, q_base *old,
                    q_base *, uintptr_t empty)
{
  old->wr_idx.store (old->cap, std::memory_order_relaxed);
  old->rd_idx.store (old->cap, std::memory_order_relaxed);

  for (size_t i = 0; i < old->cap; ++i)
    old->ptrs[i] = empty;

  old->rd_idx.store (0, std::memory_order_release);
  old->wr_idx.store (0, std::memory_order_release);
}

} // namespace detail

} // namespace xrcu
