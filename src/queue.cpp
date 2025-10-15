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
