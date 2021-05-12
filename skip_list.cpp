/* Definitions for the skip list template type.

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

#include "skip_list.hpp"
#include <cstring>
#include <new>

namespace xrcu
{

namespace detail
{

sl_node_base* sl_alloc_node (unsigned int lvl, size_t size, uintptr_t **outpp)
{
  void *p = ::operator new (size + lvl * sizeof (uintptr_t));
  *outpp = (uintptr_t *)((char *)p + size);
  memset (*outpp, 0, lvl * sizeof (uintptr_t));
  return ((sl_node_base *)p);
}

void sl_dealloc_node (void *ptr)
{
  ::operator delete (ptr);
}

} // namespace detail

} // namespace xrcu
