#ifndef __XRCU_HPP__
#define __XRCU_HPP__   1

namespace xrcu
{

// Enter a read-side critical section.
extern void enter_cs ();

// Exit a read-side critical section.
extern void exit_cs ();

// Test if the calling thread is in a read-side critical section.
extern bool in_cs ();

/* Wait until all readers have entered a quiescent state.
 * Returns false if a deadlock is detected, true otherwise. */
extern bool sync ();

// Base type for finalizable objects.
struct finalizable
{
  finalizable *fin_next = nullptr;

  virtual ~finalizable () {}
};

// Add FINP to the list of objects to be finalized after a grace period.
extern void finalize (finalizable *__finp);

struct cs_guard
{
  cs_guard ()
    {
      enter_cs ();
    }

  ~cs_guard ()
    {
      exit_cs ();
    }
};

}

#endif
