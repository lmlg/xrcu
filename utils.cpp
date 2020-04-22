/* Definitions for miscelaneous utilities.

   This file is part of xrcu.

   khipu is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include "utils.hpp"
#include <new>

namespace xrcu
{

namespace detail
{

void* alloc_wrapped (size_t size)
{
  return (::operator new (size));
}

void dealloc_wrapped (void *ptr)
{
  ::operator delete (ptr);
}

} // namespace detail

} // namespace xrcu
