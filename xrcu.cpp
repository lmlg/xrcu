#include "xrcu.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>

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

static const uintptr_t GP_CNT = 1;
static const uintptr_t GP_CTR_PHASE = ((uintptr_t)1 << (sizeof (void *) << 2));
static const uintptr_t GP_CTR_NEST_MASK = GP_CTR_PHASE - 1;

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
  std::mutex mtx;

  static const unsigned int QS_ATTEMPTS = 1000;

  registry () : counter (GP_CNT)
    {
      this->root.init_head ();
    }

  void lock ()
    {
      this->mtx.lock ();
    }

  void unlock ()
    {
      this->mtx.unlock ();
    }

  void add_tdata (td_link *lp)
    {
      this->lock ();
      lp->add (&this->root);
      this->unlock ();
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
      if (!(val & GP_CTR_NEST_MASK))
        return (rd_inactive);
      else if (!((val ^ global_reg.get_ctr ()) & GP_CTR_PHASE))
        return (rd_active);
      else
        return (rd_old);
    }

  bool in_cs () const
    {
      return ((this->get_ctr () & GP_CTR_NEST_MASK) != 0);
    }

  void flush_all ()
    {
      sync ();

      for (auto f = this->fin_objs; f != nullptr; )
        {
          auto next = f->fin_next;
          delete f;
          f = next;
        }

      this->fin_objs = nullptr;
      this->n_fins = 0;
    }

  void flush_finalizers ()
    {
      if (this->in_cs ())
        {
          this->must_flush = true;
          return;
        }

      this->flush_all ();
    }

  void finalize (finalizable *finp)
    {
      finp->fin_next = this->fin_objs;
      this->fin_objs = finp;

      if (++this->n_fins >= MAX_FINS)
        this->flush_finalizers ();
    }

  ~tl_data ()
    {
      if (!this->init)
        return;

      this->counter.store (0, std::memory_order_release);

      global_reg.lock ();
      this->del ();
      global_reg.unlock ();

      this->flush_all ();
    }
};

static thread_local tl_data tldata;

static inline tl_data*
local_data ()
{
  auto self = &tldata;
  if (!self->init)
    global_reg.add_tdata (self);

  return (self);
}

void enter_cs ()
{
  auto self = local_data ();
  std::atomic_signal_fence (std::memory_order_acq_rel);
  auto val = self->get_ctr ();
  val = (val & GP_CTR_NEST_MASK) == 0 ? global_reg.get_ctr () : val + GP_CNT;
  self->counter.store (val, std::memory_order_release);
}

void exit_cs ()
{
  auto self = local_data ();
  auto val = self->get_ctr ();
  self->counter.store (val - GP_CNT, std::memory_order_release);
  std::atomic_signal_fence (std::memory_order_acq_rel);
}

bool in_cs ()
{
  return (local_data()->in_cs ());
}

void registry::poll_readers (td_link *readers, td_link *outp, td_link *qsp)
{
  for (unsigned int loops = 0 ; ; ++loops)
    {
      td_link *next, *runp = readers;
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

      this->unlock ();
      if (loops < QS_ATTEMPTS)
        std::this_thread::yield ();
      else
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
      this->lock ();
    }
}

void registry::sync ()
{
  this->lock ();

  if (this->root.empty_p ())
    {
      this->unlock ();
      return;
    }

  td_link out, qs;
  qs.init_head ();
  out.init_head ();

  std::atomic_thread_fence (std::memory_order_acq_rel);
  poll_readers (&this->root, &out, &qs);

  this->counter.store (this->get_ctr () ^ GP_CTR_PHASE,
                       std::memory_order_release);

  poll_readers (&out, nullptr, &qs);
  qs.splice (&this->root);
  this->unlock ();
}

void sync ()
{
  global_reg.sync ();
}

} // namespace rcu

