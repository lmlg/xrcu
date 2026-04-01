#ifndef __XRCU_TESTS_STACK__
#define __XRCU_TESTS_STACK__   1

#include "xrcu/stack.hpp"
#include "utils.hpp"
#include <thread>

namespace stk_test
{

typedef xrcu::stack<std::string, test_allocator<std::string>> stack_t;

void test_single_threaded ()
{
  {
    stack_t stk;
    ASSERT (stk.empty ());
    ASSERT (!stk.top().has_value ());
  }

  {
    stack_t stk { 3, std::string ("???") };
    ASSERT (stk.size () == 3);
    for (const auto& s : stk)
      ASSERT (s == "???");
  }

  {
    stack_t stk { std::string ("abc"), std::string ("def"),
                  std::string ("ghi"), std::string ("jkl") };
    ASSERT (stk.size () == 4);
  }

  {
    std::string vals[] = { "abc", "def", "ghi" };
    stack_t stk { vals, vals + 3 };
    size_t i = 0;

    for (const auto& s : stk)
      ASSERT (s == vals[i++]);

    stack_t s2 { stk };
    ASSERT (s2 == stk);

    stack_t s3 { std::move (s2) };
    ASSERT (stk == s3);
  }

  stack_t stk;
  const int NELEM = 100;

  for (int i = 0; i < NELEM; ++i)
    stk.push (mkstr (i));

  ASSERT (*stk.top() == mkstr (NELEM - 1));

  stack_t s2;
  stk.swap (s2);

  ASSERT (stk.empty ());

  stk = s2;
  ASSERT (!stk.empty ());
  ASSERT (stk == s2);

  stk.clear ();
  ASSERT (stk.empty ());
  stk.assign (s2.begin (), s2.end ());
  ASSERT (stk == s2);

  stk.clear ();
  s2.clear ();

  stk.emplace (mkstr (10));
  s2.emplace (mkstr (20));
  ASSERT (stk < s2);

  stk.pop ();
  stk.push (mkstr (30));
  ASSERT (stk > s2);

  s2.pop ();
  s2.push (mkstr (30));
  s2.push (mkstr (40));
  ASSERT (stk <= s2);

  stk.push (mkstr (50));
  ASSERT (stk >= s2);

  ASSERT (!xrcu::in_cs ());

  {
    std::string values[] = { "a", "b", "c"};
    stack_t s3, s4;
    s3.push (values, values + 3);
    s4.assign ({ values[0], values[1], values[2] });
    ASSERT (s3 == s4);
  }
}

static void
mt_popper (stack_t *stkp, std::atomic<int> *cnt)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    if (stkp->pop().has_value ())
      cnt->fetch_add (1);
}

void test_pop_mt ()
{
  stack_t stk;
  std::atomic<int> popped { 0 };
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS * INSERTER_LOOPS; ++i)
    stk.push (mkstr (i));

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_popper, &stk, &popped));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT ((size_t)popped.load () + stk.size () ==
          (size_t)(INSERTER_THREADS * INSERTER_LOOPS));
}

static void
mt_inserter (stack_t *stkp, int index)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    stkp->push (mkstr (index * INSERTER_LOOPS + i));
}

void test_push_mt ()
{
  stack_t stk;
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_inserter, &stk, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (stk.size () == INSERTER_THREADS * INSERTER_LOOPS);
}

test_module stack_tests
{
  "stack",
  {
    { "API in a single thread", test_single_threaded },
    { "multi threaded pushes", test_push_mt },
    { "multi threaded pops", test_pop_mt }
  }
};

} // namespace stk_test

#endif
