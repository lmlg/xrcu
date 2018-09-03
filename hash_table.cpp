#include "hash_table.hpp"
#include <new>
#include <thread>
#include <cstdint>

namespace xrcu
{

namespace detail
{

ht_vector* ht_vector::make (size_t n)
{
  void *p = ::operator new (sizeof (ht_vector) + n * sizeof (uintptr_t));
  return new (p) ht_vector ((uintptr_t *)((char *)p + sizeof (ht_vector)));
}

void ht_vector::safe_destroy ()
{
  ::operator delete (this);
}

static const size_t PRIMES[] =
{
  0xb, 0x25, 0x71, 0x15b, 0x419, 0xc4d, 0x24f5, 0x6ee3, 0x14cb3, 0x3e61d,
  0xbb259, 0x23170f, 0x694531, 0x13bcf95, 0x3b36ec3, 0xb1a4c4b, 0x214ee4e3,
  0x63ecaead
#if SIZE_MAX > 0xffffffffu
  , 0x12bc60c09ull, 0x38352241dull, 0xa89f66c5bull, 0x1f9de34513ull,
  0x5ed9a9cf3bull, 0x11c8cfd6db5ull, 0x355a6f84921ull, 0xa00f4e8db65ull,
  0x1e02deba9233ull, 0x5a089c2fb69bull, 0x10e19d48f23d3ull, 0x32a4d7dad6b7dull,
  0x97ee879084279ull, 0x1c7cb96b18c76dull, 0x55762c414a564bull,
  0x1006284c3df02e3ull, 0x301278e4b9d08abull, 0x90376aae2d71a05ull,
  0x1b0a6400a8854e11ull, 0x511f2c01f98fea35ull
#endif
};

size_t prime_by_idx (size_t idx)
{
  return (PRIMES[idx]);
}

size_t find_hsize (size_t size, float mvr, size_t& pidx)
{
  intptr_t i1 = 0, i2 = sizeof (PRIMES) / sizeof (PRIMES[0]), cnt = i2 - i1;
  while (cnt > 0)
    {
      intptr_t step = cnt >> 1;
      if (PRIMES[step] < size)
        {
          i1 = step;
          cnt -= step + 1;
        }
      else
        cnt = step;
    }

  pidx = i1;
  return ((size_t)(PRIMES[i1] * mvr));
}

ht_vector* make_htvec (size_t pidx, uintptr_t key, uintptr_t val)
{
  size_t entries = PRIMES[pidx], tsize = table_idx (entries);
#ifdef XRCU_HAVE_XATOMIC_DCAS
  auto ret = ht_vector::make (tsize + 1);

  // Ensure correct alignment for double-width CAS.
  if ((uintptr_t)ret->data % (2 * sizeof (uintptr_t)) != 0)
    ++ret->data;

  --tsize;
#else
  auto ret = ht_vector::make (tsize);
#endif


  for (size_t i = TABVEC_OVERHEAD; i < tsize; i += 2)
    ret->data[i] = key, ret->data[i + 1] = val;

  ret->entries() = entries;
  ret->pidx() = pidx;

  return (ret);
}

void ht_lock::acquire ()
{
  bool sleep = false;
  int retries = 0, zv = 0;
  const int MAX_RETRIES = 100;
  const int MAX_SPINS = 1000;

  for ( ; ; zv = 0)
    {
      if (this->lock.compare_exchange_weak (zv, 1,
          std::memory_order_acquire, std::memory_order_relaxed))
        return;
      else if (sleep)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
      else
        {
          for (int i = 0; i < MAX_SPINS &&
              this->lock.load (std::memory_order_relaxed) != 0; ++i)
            xatomic_spin_nop ();

          if (++retries == MAX_RETRIES)
            sleep = true;
        }
    }
}

void ht_lock::release ()
{
  this->lock.store (0, std::memory_order_release);
}

} // namespace detail

} // namespace xrcu
