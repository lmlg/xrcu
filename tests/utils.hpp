/* Common definitions for testing.

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

#ifndef __XRCU_TESTS_UTILS__
#define __XRCU_TESTS_UTILS__   1

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#define ASSERT(Cond)   \
  do   \
    {   \
      if (!(Cond))   \
        {   \
          std::cerr << "\n\nAssertion failed: " << #Cond <<   \
            "\nLine: " << __LINE__ << "\nFile: " << __FILE__ << "\n\n";   \
          exit (EXIT_FAILURE);   \
        }   \
    }   \
  while (0)

extern std::atomic<size_t> alloc_size;

template <typename T>
struct test_allocator
{
  typedef std::true_type is_always_equal;
  typedef T value_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;

  test_allocator ()
    {
    }

  T* allocate (size_t n, const void * = nullptr)
    {
      T *ret = (T *)malloc (n * sizeof (T));
      if (! ret)
        throw std::bad_alloc ();

      alloc_size.fetch_add (n * sizeof (T));
      return (ret);
    }

  void deallocate (T *ptr, size_t n)
    {
      free (ptr);
      alloc_size.fetch_sub (n * sizeof (T));
    }

  ~test_allocator ()
    {
    }
};

struct test_fn
{
  std::string msg;
  void (*fct) (void);

  test_fn (const std::string& m, void (*f) (void)) : msg (m), fct (f)
    {
    }

  void run () const
    {
      std::cout << "Testing " << this->msg << "...";
      this->fct ();
      std::cout << " OK\n";
    }
};

std::vector<test_fn>& test_suite ()
{
  static std::vector<test_fn> ts;
  return (ts);
}

void run_tests ()
{
  for (auto& tst : test_suite ())
    tst.run ();

  std::cout << "Done\n";
}

struct test_module
{
  typedef std::pair<std::string, void (*) (void)> pair_type;

  test_module (const char *name, std::initializer_list<pair_type> tests)
    {
      std::string nm = std::string (" (") + name + ") ";
      alloc_size.store (0, std::memory_order_release);

      for (auto pair : tests)
        test_suite().push_back (test_fn (pair.first + nm, pair.second));

      size_t bytes = alloc_size.load (std::memory_order_acquire);
      if (bytes)
        std::cout << "Warning: " << bytes << " bytes were not freed\n";
    }
};

// Common constants for all tests.
static const int INSERTER_LOOPS = 2000;
static const int INSERTER_THREADS = 32;

static const int ERASER_THREADS = 16;
static const int ERASER_LOOPS = 2000;

static const int MUTATOR_KEY_SIZE = 200;
static const int MUTATOR_THREADS = 32;

inline std::string mkstr (int i)
{
  char buf[100];
  sprintf (buf, "%d", i);
  return (std::string (buf));
}

#endif
