#ifndef __XRCU_TESTS_XRCU__
#define __XRCU_TESTS_XRCU__   1

#include "xrcu/xrcu.hpp"
#include "utils.hpp"
#include <thread>
#include <atomic>

namespace xrcu_test
{

static std::atomic<int> G_CNT;

struct tst_fin : public xrcu::finalizable
{
  ~tst_fin ()
    {
      G_CNT.fetch_add (1);
    }
};

void test_xrcu ()
{
  xrcu::enter_cs ();
  ASSERT (xrcu::in_cs ());
  xrcu::exit_cs ();
  ASSERT (!xrcu::in_cs ());

  xrcu::enter_cs ();
  tst_fin *p = new tst_fin ();
  finalize (p);
  xrcu::flush_finalizers ();
  ASSERT (G_CNT.load () == 0);
  xrcu::exit_cs ();
  ASSERT (G_CNT.load () == 1);
}

static void
mt_xrcu (tst_fin *fp)
{
  xrcu::cs_guard g;
  finalize (fp);
}

void test_xrcu_mt ()
{
  const int NTHREADS = 100;
  std::vector<std::thread> thrs;

  G_CNT.store (0);

  for (int i = 0; i < NTHREADS; ++i)
    thrs.push_back (std::thread (mt_xrcu, new tst_fin ()));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (G_CNT.load () == NTHREADS);
}

test_module xrcu_tests
{
  "xrcu",
  {
    { "API", test_xrcu },
    { "API with multiple threads", test_xrcu_mt }
  }
};

} // namespace xrcu_test

#endif

