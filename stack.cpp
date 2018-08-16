#include "stack.hpp"
#include <atomic>

namespace xrcu
{

struct sb_impl
{
  std::atomic<size_t> size;
  std::atomic<stack_node_base *> root;
};

static_assert (sizeof (sb_impl) <= sizeof (((stack_base *)nullptr)->buf) &&
  (alignof (sb_impl) % alignof (((stack_base *)nullptr))->buf) == 0,
  "stack_base opaque buffer has the wrong size or alignment");

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

void stack_base::push_node (stack_node_base *nodep)
{
  for (auto sb = get_impl (this->buf) ; ; )
    {
      nodep->next = sb->root.load (std::memory_order_relaxed);
      if (sb->root.compare_exchange_weak (nodep->next, nodep,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        break;
    }

  sb->size.fetch_add (1, std::memory_order_relaxed);
}

stack_node_base* stack_base::pop_node ()
{
  cs_guard g;

  for (auto sb = get_impl (this->buf) ; ; )
    {
      auto np = sb->root.load (std::memory_order_relaxed);
      if (np == nullptr)
        return (np);
      else if (sb->root.compare_exchange_weak (np, np->next,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        {
          sb->size.fetch_sub (1, std::memory_order_relaxed);
          return (np);
        }
    }
}

bool stack_base::empty () const
{
  auto sb = get_impl (this->buf);
  return (sb->root.load (std::memory_order_relaxed) == nullptr);
}

size_t stack_base::size () const
{
  auto sb = get_impl (this->buf);
  return (sb->size.load (std::memory_order_relaxed));
}

}
