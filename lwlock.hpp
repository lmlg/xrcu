#ifndef __XRCU_LWLOCK_HPP__
#define __XRCU_LWLOCK_HPP__   1

#include <cstdint>

namespace xrcu
{

struct lwlock
{
  uintptr_t lock = 0;

  void acquire ();
  void release ();
};

}

#endif
