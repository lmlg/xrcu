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

// Wait until all readers have entered a quiescent state.
extern void sync ();

struct finalizable
{
  finalizable *fin_next;

  virtual ~finalizable () {}
};

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
