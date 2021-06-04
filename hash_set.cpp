/* Definitions for the hash set template type.

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

#include "hash_set.hpp"
#include <new>

namespace xrcu
{

namespace detail
{

hs_vector* hs_vector::alloc (size_t n, uintptr_t fill)
{
  void *p = ::operator new (sizeof (hs_vector) + n * sizeof (uintptr_t));
  uintptr_t *ep = (uintptr_t *)((char *)p + sizeof (hs_vector));

  for (size_t i = 0; i < n; ++i)
    ep[i] = fill;

  return (new (p) hs_vector (ep, n));
}

void hs_vector::safe_destroy ()
{
  ::operator delete (this);
}

hs_sentry::hs_sentry (lwlock *lp, uintptr_t xb) : lock (lp), xbit (~xb)
{
  this->lock->acquire ();
}

hs_sentry::~hs_sentry ()
{
  if (this->vector)
    for (size_t i = 0; i < this->vector->entries; ++i)
      xatomic_and (&this->vector->data[i], this->xbit);

  this->lock->release ();
}

} // namespace detail

} // namespace xrcu
