#ifndef __XRCU_TESTS_QUEUE__
#define __XRCU_TESTS_QUEUE__   1

#include "../queue.hpp"
#include "utils.hpp"
#include <thread>

namespace q_test
{

typedef xrcu::queue<std::string> queue_t;

void test_single_threaded ()
{
  {
    queue_t q;
    ASSERT (q.empty ());
    ASSERT (!q.back().has_value ());
  }

  {
    queue_t q { 3, std::string ("???") };
    ASSERT (q.size () == 3);
    for (const auto& s : q)
      ASSERT (s == "???");
  }

  {
    queue_t q { std::string ("abc"), std::string ("def"),
                std::string ("ghi"), std::string ("jkl") };
    ASSERT (q.size () == 4);

    queue_t q2 { q };
    ASSERT (q == q2);

    queue_t q3 { std::move (q2) };
    ASSERT (q3 == q);
  }

  queue_t q;
  const int NELEM = 100;

  for (int i = 0; i < NELEM; ++i)
    q.push (mkstr (i));

  ASSERT (*q.pop () == mkstr (NELEM - 1));
}

}
