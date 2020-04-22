/* Declarations for lightweight locks.

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

#ifndef __XRCU_LWLOCK_HPP__
#define __XRCU_LWLOCK_HPP__   1

#include <cstdint>

namespace xrcu
{

/* Lightweight lock used when including <mutex> is overkill. Should be just as
 * performant in certain platforms (i.e: Those that support something akin to
 * Linux's futexes), but much lighter space-wise. */
struct lwlock
{
  uintptr_t lock = 0;

  void acquire ();
  void release ();

  lwlock () = default;

  lwlock (const lwlock&) = delete;
  void operator= (const lwlock&) = delete;
};

}

#endif
