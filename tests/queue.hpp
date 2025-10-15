#ifndef __XRCU_TESTS_QUEUE__
#define __XRCU_TESTS_QUEUE__   1

#include "xrcu/queue.hpp"
#include "utils.hpp"
#include <thread>

namespace q_test
{

typedef xrcu::queue<std::string, test_allocator<std::string>> queue_t;

void test_single_threaded ()
{
  {
    queue_t q;
    ASSERT (q.empty ());
    ASSERT (!q.front().has_value ());
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

  ASSERT (*q.pop () == mkstr (0));

  queue_t q2;
  q.swap (q2);

  ASSERT (q.empty ());

  q = q2;
  ASSERT (!q.empty ());
  ASSERT (q == q2);

  q.clear ();
  ASSERT (q.empty ());
  q.assign (q2.begin (), q2.end ());
  ASSERT (q == q2);

  q.clear ();
  q2.clear ();

  q.push (mkstr (10));
  q2.push (mkstr (20));
  ASSERT (q < q2);

  q.pop ();
  q.push (mkstr (30));
  ASSERT (q > q2);

  q2.pop ();
  q2.push (mkstr (30));
  q2.push (mkstr (40));
  ASSERT (q <= q2);

  q.push (mkstr (50));
  ASSERT (q >= q2);
}

static void
mt_inserter (queue_t *qp, int index)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    qp->push (mkstr (index * INSERTER_LOOPS + i));
}

void test_push_mt ()
{
  queue_t q;
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_inserter, &q, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (q.size () == INSERTER_THREADS * INSERTER_LOOPS);
}

test_module queue_tests
{
  "queue",
  {
    { "API in a single thread", test_single_threaded },
    { "multi threaded pushes", test_push_mt }
  }
};

}

#endif
