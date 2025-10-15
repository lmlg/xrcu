/* Definitions for the RCU API.

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

#include "xrcu/lwlock.hpp"
#include "xrcu/xatomic.hpp"

#if (defined (linux) || defined (__linux) || defined (__linux__)) &&   \
    defined (__BYTE_ORDER__)

#include <linux/futex.h>
#include <unistd.h>
#include <syscall.h>

#ifndef FUTEX_PRIVATE_FLAG
#  define FUTEX_PRIVATE_FLAG   0
#endif

template <bool Wide>
void* futex_ptr (void *ptr)
{
  return (ptr);
}

template <>
void* futex_ptr<true> (void *ptr)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (ptr);
#else
  return ((int *)ptr + 1);
#endif
}

static inline void
lwlock_acquire (uintptr_t *ptr)
{
  const int MAX_SPINS = 1000;

  if (xrcu::xatomic_cas_bool (ptr, 0, 1))
    return;

  while (true)
    {
      for (int i = 0; i < MAX_SPINS && *ptr != 0; ++i)
        xrcu::xatomic_spin_nop ();

      if (xrcu::xatomic_swap (ptr, 2) == 0)
        return;

      syscall (SYS_futex, futex_ptr<sizeof (int) < sizeof (void *)> (ptr),
        (long)(FUTEX_WAIT | FUTEX_PRIVATE_FLAG), 2l, 0ul);
    }
}

static inline void
lwlock_release (uintptr_t *ptr)
{
  if (xrcu::xatomic_swap (ptr, 0) != 1)
    syscall (SYS_futex, futex_ptr<sizeof (int) < sizeof (void *)> (ptr),
      (long)(FUTEX_WAKE | FUTEX_PRIVATE_FLAG), 1ul);
}

#else

#include <thread>
#include <atomic>

static inline void
lwlock_acquire (uintptr_t *ptr)
{
  const int MAX_SPINS = 1000;

  while (true)
    {
      if (xrcu::xatomic_cas_bool (ptr, 0, 1))
        return;

      for (int i = 0; i < MAX_SPINS && *ptr != 0; ++i)
        xrcu::xatomic_spin_nop ();

      if (xrcu::xatomic_swap (ptr, 1) == 0)
        break;

      std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

static inline void
lwlock_release (uintptr_t *ptr)
{
  *ptr = 0;
  std::atomic_thread_fence (std::memory_order_release);
}

#endif

namespace xrcu
{

void lwlock::acquire ()
{
  lwlock_acquire (&this->lock);
}

void lwlock::release ()
{
  lwlock_release (&this->lock);
}

} // namespace xrcu
