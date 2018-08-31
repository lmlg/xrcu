#include "stack.hpp"
#include "xatomic.hpp"
#include <atomic>

namespace xrcu
{

namespace detail
{

struct sb_impl
{
  std::atomic<stack_node_base *> root;
};

static_assert (sizeof (sb_impl) <= sizeof (((stack_base *)nullptr)->buf),
  "stack_base opaque buffer is too small");

static inline sb_impl*
get_impl (void *buf)
{
  return ((sb_impl *)buf);
}

static inline const sb_impl*
get_impl (const void *buf)
{
  return ((const sb_impl *)buf);
}

stack_base::stack_base ()
{
  auto sb = get_impl (this->buf);
  sb->root.store (nullptr, std::memory_order_relaxed);
}

stack_node_base* stack_base::root () const
{
  return (get_impl(this->buf)->root.load (std::memory_order_relaxed));
}

static stack_node_base* const NODE_SPIN = (stack_node_base *)1;

void stack_base::push_node (stack_node_base *nodep)
{
  auto sb = get_impl (this->buf);

  while (true)
    {
      nodep->next = sb->root.load (std::memory_order_relaxed);
      if (nodep->next == NODE_SPIN)
        {
          xatomic_spin_nop ();
          continue;
        }

      nodep->size = 1 + (nodep->next ? nodep->next->size : 0);

      if (sb->root.compare_exchange_weak (nodep->next, nodep,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        break;
    }
}

stack_node_base* stack_base::pop_node ()
{
  cs_guard g;

  for (auto sb = get_impl (this->buf) ; ; )
    {
      auto np = sb->root.load (std::memory_order_relaxed);
      if (np == NODE_SPIN)
        {
          xatomic_spin_nop ();
          continue;
        }

      if (np == nullptr || sb->root.compare_exchange_weak (np, np->next,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        return (np);
    }
}

bool stack_base::empty () const
{
  return (this->root () == nullptr);
}

size_t stack_base::size () const
{
  auto tmp = this->root ();
  return (tmp ? tmp->size : 0);
}

void stack_base::swap (stack_base& right)
{
  auto lp = get_impl (this->buf);
  auto rp = get_impl (right.buf);

  auto ln = lp->root.exchange (NODE_SPIN, std::memory_order_relaxed);
  auto rn = lp->root.exchange (NODE_SPIN, std::memory_order_relaxed);

  lp->root.store (rn, std::memory_order_release);
  rp->root.store (ln, std::memory_order_release);
}

void stack_base::destroy (void (*fct) (stack_node_base *))
{
  auto runp = this->root ();
  while (runp != nullptr)
    {
      auto next = runp->next;
      fct (runp), runp = next;
    }

  get_impl(this->buf)->root.store (nullptr, std::memory_order_relaxed);
}

} } // namespaces

