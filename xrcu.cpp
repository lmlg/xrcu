#include "xrcu.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace xrcu
{

struct td_link
{
  td_link *next;
  td_link *prev;

  void init_head ()
    {
      this->next = this->prev = this;
    }

  void add (td_link *headp)
    { // Add self to HEADP.
      this->next = headp->next;
      this->prev = headp;
      headp->next->prev = this;
      headp->next = this;
    }

  void del ()
    { // Unlink self.
      this->next->prev = this->prev;
      this->prev->next = this->next;
    }

  bool empty_p () const
    {
      return (this == this->next);
    }

  void splice (td_link *dst)
    {
      if (this->empty_p ())
        return;

      this->next->prev = dst;
      this->prev->next = dst->next;
      dst->next->prev = this->prev;
      dst->next = this->next;
    }
};

static const uintptr_t GP_PHASE_BIT =
  (uintptr_t)1 << (sizeof (uintptr_t) * 8 - 1);

static const uintptr_t GP_NEST_MASK = GP_PHASE_BIT - 1;

// Possible states for a reader thread.
enum
{
  rd_active,
  rd_inactive,
  rd_old
};

struct registry
{
  std::atomic_uintptr_t counter;
  td_link root;
  std::mutex td_mtx;
  std::mutex gp_mtx;

  static const unsigned int QS_ATTEMPTS = 1000;

  registry () : counter (1)
    {
      this->root.init_head ();
    }

  void add_tdata (td_link *lp)
    {
      this->td_mtx.lock ();
      lp->add (&this->root);
      this->td_mtx.unlock ();
    }

  uintptr_t get_ctr () const
    {
      return (this->counter.load (std::memory_order_relaxed));
    }

  void poll_readers (td_link *, td_link *, td_link *);

  void sync ();
};

static registry global_reg;

// Maximum number of pending finalizers before flushing.

#ifndef XRCU_MAX_FINS
static const unsigned int MAX_FINS = 1000;
#else
static const unsigned int MAX_FINS = XRCU_MAX_FINS;
#endif

struct tl_data : public td_link
{
  bool init;
  bool must_flush;
  unsigned int n_fins;
  std::atomic_uintptr_t counter;
  finalizable *fin_objs;

  uintptr_t get_ctr () const
    {
      return (this->counter.load (std::memory_order_relaxed));
    }

  int state () const
    {
      auto val = this->counter.load (std::memory_order_acquire);
      if (!(val & GP_NEST_MASK))
        return (rd_inactive);
      else if (!((val ^ global_reg.get_ctr ()) & GP_PHASE_BIT))
        return (rd_active);
      else
        return (rd_old);
    }

  bool in_cs () const
    {
      return ((this->get_ctr () & GP_NEST_MASK) != 0);
    }

  void flush_all ()
    {
      sync ();

      for (auto f = this->fin_objs; f != nullptr; )
        {
          auto next = f->fin_next;
          f->safe_destroy ();
          f = next;
        }

      this->fin_objs = nullptr;
      this->n_fins = 0;
    }

  void finalize (finalizable *finp)
    {
      finp->fin_next = this->fin_objs;
      this->fin_objs = finp;

      if (++this->n_fins < MAX_FINS)
        ;
      else if (this->in_cs ())
        /* Can't reclaim memory since we are in a critical section.
         * Set the flag to do it ASAP. */
        this->must_flush = true;
      else
        this->flush_all ();
    }

  ~tl_data ()
    {
      if (!this->init)
        return;

      this->counter.store (0, std::memory_order_release);

      if (this->n_fins > 0)
        this->flush_all ();

      global_reg.td_mtx.lock ();
      this->del ();
      global_reg.td_mtx.unlock ();
    }
};

static thread_local tl_data tldata {};

static inline tl_data*
local_data ()
{
  auto self = &tldata;
  if (!self->init)
    {
      global_reg.add_tdata (self);
      self->init = true;
    }

  return (self);
}

void enter_cs ()
{
  auto self = local_data ();
  std::atomic_signal_fence (std::memory_order_acq_rel);
  auto val = self->get_ctr ();
  val = (val & GP_NEST_MASK) == 0 ? global_reg.get_ctr () : val + 1;
  self->counter.store (val, std::memory_order_release);
}

void exit_cs ()
{
  auto self = local_data ();
  auto val = self->get_ctr ();
  self->counter.store (val - 1, std::memory_order_release);
  std::atomic_signal_fence (std::memory_order_acq_rel);

  if (self->must_flush && !self->in_cs ())
    self->flush_all ();
}

bool in_cs ()
{
  return (local_data()->in_cs ());
}

void registry::poll_readers (td_link *readers, td_link *outp, td_link *qsp)
{
  for (unsigned int loops = 0 ; ; ++loops)
    {
      td_link *next, *runp = readers->next;
      for (; runp != readers; runp = next)
        {
          next = runp->next;
          switch (((tl_data *)runp)->state ())
            {
              case rd_active:
                if (outp != nullptr)
                  {
                    runp->del ();
                    runp->add (outp);
                    break;
                  }

              // Fallthrough.
              case rd_inactive:
                runp->del ();
                runp->add (qsp);
                break;

              default:
                break;
            }
        }

      if (readers->empty_p ())
        break;

      this->td_mtx.unlock ();
      if (loops < QS_ATTEMPTS)
        std::this_thread::yield ();
      else
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
      this->td_mtx.lock ();
    }
}

void registry::sync ()
{
  this->gp_mtx.lock ();
  this->td_mtx.lock ();

  if (this->root.empty_p ())
    {
      this->td_mtx.unlock ();
      this->gp_mtx.unlock ();
      return;
    }

  td_link out, qs;
  qs.init_head ();
  out.init_head ();

  std::atomic_thread_fence (std::memory_order_acq_rel);
  poll_readers (&this->root, &out, &qs);

  this->counter.store (this->get_ctr () ^ GP_PHASE_BIT,
                       std::memory_order_relaxed);

  poll_readers (&out, nullptr, &qs);
  qs.splice (&this->root);
  this->td_mtx.unlock ();
  this->gp_mtx.unlock ();
}

bool sync ()
{
  if (in_cs ())
    return (false);

  global_reg.sync ();
  return (true);
}

void finalize (finalizable *finp)
{
  local_data()->finalize (finp);
}

} // namespace rcu

