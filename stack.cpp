#include "stack.hpp"
#include "xatomic.hpp"
#include <atomic>

namespace xrcu
{

namespace detail
{

static const uintptr_t SPIN_BIT = 1;

static inline bool
node_spinning_p (const stack_node_base *np)
{
  return (((uintptr_t)np) & SPIN_BIT);
}

stack_node_base* stack_base::root () const
{
  uintptr_t ret = (uintptr_t)this->rnode.load (std::memory_order_relaxed);
  return (((stack_node_base *)(ret & ~SPIN_BIT)));
}

void stack_base::push_node (stack_node_base *nodep)
{
  while (true)
    {
      nodep->next = this->rnode.load (std::memory_order_relaxed);

      if (!node_spinning_p (nodep->next) &&
          this->rnode.compare_exchange_weak (nodep->next, nodep,
            std::memory_order_acq_rel, std::memory_order_relaxed))
        {
          this->size.fetch_add (1, std::memory_order_relaxed);
          break;
        }

      xatomic_spin_nop ();
    }
}

void stack_base::push_nodes (stack_node_base *nodep,
  stack_node_base **outp, size_t cnt)
{
  while (true)
    {
      auto tmp = this->rnode.load (std::memory_order_relaxed);
      if (!node_spinning_p (tmp))
        {
          *outp = tmp;
          if (this->rnode.compare_exchange_weak (tmp, nodep,
              std::memory_order_acq_rel, std::memory_order_relaxed))
            {
              this->size.fetch_add (cnt, std::memory_order_relaxed);
              break;
            }
        }

      xatomic_spin_nop ();
    }
}

stack_node_base* stack_base::pop_node ()
{
  while (true)
    {
      auto np = this->rnode.load (std::memory_order_relaxed);

      if (node_spinning_p (np))
        ;
      else if (np == nullptr)
        return (np);
      else if (this->rnode.compare_exchange_weak (np, np->next,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        {
          this->size.fetch_sub (1, std::memory_order_release);
          return (np);
        }

      xatomic_spin_nop ();
    }
}

static inline stack_node_base*
set_spin (std::atomic<stack_node_base *>& rn)
{
  while (true)
    {
      stack_node_base *tmp = rn.load (std::memory_order_relaxed);
      if (!node_spinning_p (tmp) &&
          rn.compare_exchange_weak (tmp,
            (stack_node_base *)((uintptr_t)tmp | SPIN_BIT),
            std::memory_order_acq_rel, std::memory_order_relaxed))
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

