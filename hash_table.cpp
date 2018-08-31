#include "hash_table.hpp"
#include <new>
#include <thread>

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

  while (true)
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
          sleep = ++retries == MAX_RETRIES;
        }

      zv = 0;
    }
}

void ht_lock::release ()
{
  this->lock.store (0, std::memory_order_release);
}

} // namespace detail

} // namespace xrcu
