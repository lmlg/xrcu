#include "lwlock.hpp"
#include "xatomic.hpp"

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
        ;

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
  const int MAX_RETRIES = 100;
  const int MAX_NSPINS = 1000;
  int retries = 0;

  while (true)
    {
      if (xrcu::xatomic_cas_bool (ptr, 0, 1))
        return;

      for (int i = 0; i < MAX_SPINS && *ptr != 0; ++i)
        xrcu::xatomic_spin_nop ();

      if (++retries == MAX_RETRIES)
        {
          std::this_thread::sleep_for (std::chrono::milliseconds (1));
          retries = 0;
        }
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
