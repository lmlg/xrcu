#include "stack.hpp"
#include "xatomic.hpp"
#include <atomic>

namespace xrcu
{

namespace detail
{

static stack_node_base* const NODE_SPIN = (stack_node_base *)1;

void stack_base::push_node (stack_node_base *nodep)
{
  while (true)
    {
      nodep->next = this->rnode.load (std::memory_order_relaxed);
      if (nodep->next == NODE_SPIN)
        {
          xatomic_spin_nop ();
          continue;
        }

      if (this->rnode.compare_exchange_weak (nodep->next, nodep,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        {
          this->size.fetch_add (1, std::memory_order_relaxed);
          break;
        }
    }
}

stack_node_base* stack_base::pop_node ()
{
  cs_guard g;

  while (true)
    {
      auto np = this->rnode.load (std::memory_order_relaxed);

      if (np == NODE_SPIN)
        {
          xatomic_spin_nop ();
          continue;
        }
      else if (np == nullptr)
        return (np);
      else if (this->rnode.compare_exchange_weak (np, np->next,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        {
          this->size.fetch_sub (1, std::memory_order_release);
          return (np);
        }
    }
}

static inline stack_node_base*
set_spin (std::atomic<stack_node_base *>& rn)
{
  while (true)
    {
      stack_node_base *tmp = rn.load (std::memory_order_relaxed);
      if (tmp != NODE_SPIN && rn.compare_exchange_weak (tmp, NODE_SPIN,
          std::memory_order_acq_rel, std::memory_order_release))
        return (tmp);

      xatomic_spin_nop ();
    }
}

void stack_base::swap (stack_base& right)
{
  // Prevent any further modifications.
  auto ln = set_spin (this->rnode), rn = set_spin (right.rnode);

  // Swap the sizes.
  size_t sz = this->size.load (std::memory_order_relaxed);
  this->size.store (right.size.load (std::memory_order_relaxed),
                    std::memory_order_release);
  right.size.store (sz, std::memory_order_release);

  // And finally, swap the root nodes.
  this->rnode.store (rn, std::memory_order_release);
  right.rnode.store (ln, std::memory_order_release);
}

} } // namespaces

