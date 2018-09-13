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
