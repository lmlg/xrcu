/* Definitions for the hash table template type.

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

#include "hash_table.hpp"
#include <new>
#include <thread>
#include <cstdint>

namespace xrcu
{

namespace detail
{

inline ht_vector* ht_vector_make (size_t n)
{
  void *p = ::operator new (sizeof (ht_vector) + n * sizeof (uintptr_t));
  return (new (p) ht_vector ((uintptr_t *)((char *)p + sizeof (ht_vector))));
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

size_t find_hsize (size_t size, float mvr, size_t& pidx)
{
  intptr_t i1 = 0, i2 = sizeof (PRIMES) / sizeof (PRIMES[0]);
  while (i1 < i2)
    {
      intptr_t step = (i1 + i2) >> 1;
      if (PRIMES[step] < size)
        i1 = step + 1;
      else
        i2 = step;
    }

  pidx = i1;
  return ((size_t)(PRIMES[i1] * mvr));
}

ht_vector* make_htvec (size_t pidx, uintptr_t key, uintptr_t val)
{
  size_t entries = PRIMES[pidx], tsize = table_idx (entries);
#ifdef XRCU_HAVE_XATOMIC_DCAS
  auto ret = ht_vector_make (tsize + 1);

  // Ensure correct alignment for double-width CAS.
  if ((uintptr_t)ret->data % (2 * sizeof (uintptr_t)) != 0)
    ++ret->data;
#else
  auto ret = ht_vector_make (tsize);
#endif

  for (size_t i = 0; i < tsize; i += 2)
    ret->data[i] = key, ret->data[i + 1] = val;

  ret->entries = entries;
  ret->pidx = pidx;

  return (ret);
}

} // namespace detail

} // namespace xrcu
