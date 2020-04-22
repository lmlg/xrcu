/* Declarations for atomic operations.

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

#ifndef __XRCU_XATOMIC_HPP__
#define __XRCU_XATOMIC_HPP__   1

#include <cstdint>

/*
 * This file defines an interface for atomic operations that isn't (quite)
 * achievable with the C++ standard atomic API. Basically, instead of using
 * a template class, we use raw pointers.
 *
 * This interface is needed because for some inexplicable reason, it is not
 * possible to get a pointer to the underlying integer in the std::atomic
 * interface (it may not even exist as such).
 *
 * While we are at it, we also define a few additional operations that are
 * not present in the standard (double CAS, atomic spin).
 *
 * Note that these aren't template functions; we only require these atomic
 * ops to work on pointer-sized values, so we don't bother with anything else.
 */

namespace xrcu
{

#if (defined (__GNUC__) && (__GNUC__ > 4 ||   \
    (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))) || (defined (__clang__) &&   \
    defined (__clang_major__) && (__clang_major__ >= 4 ||   \
      (__clang_major__ == 3 && __clang_minor__ >= 8)))

inline uintptr_t
xatomic_cas (uintptr_t *ptr, uintptr_t exp, uintptr_t nval)
{
  __atomic_compare_exchange_n (ptr, &exp, nval, 0,
    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
  return (exp);
}

inline uintptr_t
xatomic_or (uintptr_t *ptr, uintptr_t val)
{
  return (__atomic_fetch_or (ptr, val, __ATOMIC_ACQ_REL));
}

inline void
xatomic_and (uintptr_t *ptr, uintptr_t val)
{
  (void)__atomic_and_fetch (ptr, val, __ATOMIC_ACQ_REL);
}

inline uintptr_t
xatomic_swap (uintptr_t *ptr, uintptr_t val)
{
  return (__atomic_exchange_n (ptr, val, __ATOMIC_ACQ_REL));
}

inline uintptr_t
xatomic_add (uintptr_t *ptr, intptr_t val)
{
  return (__atomic_fetch_add (ptr, val, __ATOMIC_ACQ_REL));
}

#else

#include <atomic>

static_assert (sizeof (uintptr_t) == sizeof (std::atomic_uintptr_t) &&
  alignof (uintptr_t) == alignof (std::atomic_uintptr_t),
  "unsupported compiler (uintptr_t and atomic_uintptr_t mismatch)");

inline uintptr_t
xatomic_cas (uintptr_t *ptr, uintptr_t exp, uintptr_t nval)
{
  reinterpret_cast<std::atomic_uintptr_t&>(ptr).compare_exchange_weak
    (exp, nval, std::memory_order_acq_rel, std::memory_order_relaxed);
  return (exp);
}

inline uintptr_t
xatomic_swap (uintptr_t *ptr, uintptr_t val)
{
  return (reinterpret_cast<std::atomic_uintptr_t&>(ptr).exchange
    (ptr, val, std::memory_order_acq_rel));
}

inline uintptr_t
xatomic_add (uintptr_t *ptr, intptr_t val)
{
  return (reinterpret_cast<std::atomic_uintptr_t&>(ptr).fetch_add
    (ptr, val, std::memory_order_acq_rel));
}

inline uintptr_t
xatomic_or (uintptr_t *ptr, uintptr_t val)
{
  while (true)
    {
      uintptr_t ret = *ptr;
      if (xatomic_cas (ptr, ret, ret | val) == ret)
        return (ret);

      xatomic_spin_nop ();
    }
}

inline void
xatomic_and (uintptr_t *ptr, uintptr_t val)
{
  while (true)
    {
      uintptr_t ret = *ptr;
      if (xatomic_cas (ptr, ret, ret & val) == ret)
        return;

      xatomic_spin_nop ();
    }
}

#endif

#if defined (__GNUC__)

#  if defined (__i386__) || defined (__x86_64__)

inline void
xatomic_spin_nop ()
{
  __asm__ __volatile__ ("pause" : : : "memory");
}

#  elif defined (__aarch64__) || defined (__arm__)

inline void
xatomic_spin_nop ()
{
  __asm__ __volatile__ ("wfe" : : : "memory");
}

#  else

inline void
xatomic_spin_nop ()
{
  __atomic_thread_fence (__ATOMIC_ACQUIRE);
}

#  endif

#else

#include <atomic>

inline void
xatomic_spin_nop ()
{
  std::atomic_thread_fence (std::memory_order_acquire);
}

#endif

inline bool
xatomic_cas_bool (uintptr_t *ptr, uintptr_t exp, uintptr_t nval)
{
  return (xatomic_cas (ptr, exp, nval) == exp);
}

// Try to define double-width CAS.

#if defined (__GNUC__)

#  if defined (__amd64) || defined (__amd64__) ||   \
      defined (__x86_64) || defined (__x86_64__)

#    define XRCU_HAVE_XATOMIC_DCAS

#    if defined (_ILP32) || defined (__ILP32__)

inline bool
xatomic_dcas_bool (uintptr_t *ptr, uintptr_t elo,
  uintptr_t ehi, uintptr_t nlo, uintptr_t nhi)
{
  uint64_t exp = ((uint64_t)ehi << 32) | elo;
  uint64_t nval = ((uint64_t)nhi << 32) | nlo;
  return (__atomic_compare_exchange_n ((uint64_t *)ptr,
    &exp, nval, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

#    else

inline bool
xatomic_dcas_bool (uintptr_t *ptr, uintptr_t elo,
  uintptr_t ehi, uintptr_t nlo, uintptr_t nhi)
{
  char r;

  __asm__ __volatile__
    (
      "lock; cmpxchg16b %0\n\t"
      "setz %1"
      : "+m" (*ptr), "=q" (r)
      : "d" (ehi), "a" (elo),
        "c" (nhi), "b" (nlo)
      : "memory"
    );

  return ((bool)r);
}

#    endif   // ILP32

#  elif defined (__i386) || defined (__i386__)

#    define XRCU_HAVE_XATOMIC_DCAS

#    if defined (__PIC__) && __GNUC__ < 5

inline bool
xatomic_dcas_bool (uintptr_t *ptr, uintptr_t elo,
  uintptr_t ehi, uintptr_t nlo, uintptr_t nhi)
{
  uintptr_t s;
  char r;

  __asm__ __volatile__
    (
      "movl %%ebx, %2\n\t"
      "leal %0, %%edi\n\t"
      "movl %7, %%ebx\n\t"
      "lock; cmpxchg8b (%%edi)\n\t"
      "movl %2, %%ebx\n\t"
      "setz %1"
      : "=m" (*ptr), "=a" (r), "=m" (s)
      : "m" (*ptr), "d" (ehi), "a" (elo),
        "c" (nhi), "m" (nlo)
      : "%edi", "memory"
    );

  return ((bool)r);
}

#    else

inline bool
xatomic_dcas_bool (uintptr_t *ptr, uintptr_t elo,
  uintptr_t ehi, uintptr_t nlo, uintptr_t nhi)
{
  char r;
  __asm__ __volatile__
    (
      "lock; cmpxchg8b %0\n\t"
      "setz %1"
      : "+m" (*ptr), "=a" (r)
      : "d" (ehi), "a" (elo),
        "c" (nhi), "b" (nlo)
      : "memory"
    );

  return ((bool)r);
}

#    endif // PIC.

#  elif (defined (__arm__) || defined (__thumb__)) &&   \
       ((!defined (__thumb__) || (defined (__thumb2__) &&   \
         !defined (__ARM_ARCH_7__)) && !defined (__ARCH_ARM_7M__) &&   \
         !defined (__ARM_ARCH_7EM__)) && (!defined (__clang__) ||   \
        (__clang_major__ == 3 && __clang_minor__ >= 3)))

#    define XRCU_HAVE_XATOMIC_DCAS

inline bool
xatomic_dcas_bool (uintptr_t *ptr, uintptr_t elo,
  uintptr_t ehi, uintptr_t nlo, uintptr_t nhi)
{
  uint64_t qv = ((uint64_t)ehi << 32) | elo;
  uint64_t nv = ((uint64_t)nhi << 32) | nlo;

  while (true)
    {
      uint64_t tmp;
      __asm__ __volatile__
        (
          "ldrexd %0, %H0, [%1]"
          : "=&r" (tmp) : "r" (ptr)
        );

      if (tmp != qv)
        return (false);

      int r;
      __asm__ __volatile__
        (
          "strexd %0, %3, %H3, [%2]"
          : "=&r" (r), "+m" (*ptr)
          : "r" (ptr), "r" (nv)
          : "cc"
        );

      if (r == 0)
        return (true);
    }
}

#  elif defined (__aarch64__)

#    define XRCU_HAVE_XATOMIC_DCAS

inline bool
xatomic_dcas_bool (uintptr_t *ptr, uintptr_t elo,
  uintptr_t ehi, uintptr_t nlo, uintptr_t nhi)
{
  while (true)
    {
      uintptr_t t1, t2;
      __asm__ __volatile__
        (
          "ldaxp %0, %1, %2"
          : "=&r" (t1), "=&r" (t2)
          : "Q" (*ptr)
        );

      if (t1 != elo || t2 != ehi)
        return (false);

      int r;
      __asm__ __volatile__
        (
          "stxp %w0, %2, %3, %1"
          : "=&r" (r), "=Q" (*ptr)
          : "r" (nlo), "r" (nhi)
        );

      if (r == 0)
        return (true);
    }
}

#  endif

#endif

} // namespace xrcu

#endif
