/* Declarations for memory-related interfaces.

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

#ifndef __XRCU_MEMORY_HPP__
#define __XRCU_MEMORY_HPP__   1

#include <cstddef>
#include <cstdint>

namespace xrcu
{

template <typename Alloc>
uintptr_t* alloc_uptrs (size_t tsize, size_t nr_uptrs,
                        size_t *upp = nullptr)
{
  size_t total = tsize + nr_uptrs * sizeof (uintptr_t);
  size_t uptrs = (total / sizeof (uintptr_t)) +
                 ((total % sizeof (uintptr_t)) != 0);
  if (upp)
    *upp = uptrs;

  return (Alloc().allocate (uptrs));
}

template <typename Alloc>
void dealloc_uptrs (void *base, void *end)
{
  size_t total = (char *)end - (char *)base;
  Alloc().deallocate ((uintptr_t *)base, total / sizeof (uintptr_t));
}

} // namespace xrcu

#endif
