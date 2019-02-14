#ifndef __XRCU_TESTS_UTILS__
#define __XRCU_TESTS_UTILS__   1

#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <initializer_list>
#include <cstdlib>

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
      std::string nm = " (";
      nm += name;
      nm += ") ";

      for (auto pair : tests)
        test_suite().push_back (test_fn (pair.first + nm, pair.second));
    }
};

// Common constants for all tests.
static const int INSERTER_LOOPS = 1000;
static const int INSERTER_THREADS = 16;

static const int ERASER_THREADS = 8;
static const int ERASER_LOOPS = 1000;

static const int MUTATOR_KEY_SIZE = 100;
static const int MUTATOR_THREADS = 16;

inline std::string mkstr (int i)
{
  char buf[100];
  sprintf (buf, "%d", i);
  return (std::string (buf));
}

#endif
