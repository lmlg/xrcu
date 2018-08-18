#include "stack.hpp"
#include <atomic>

namespace xrcu
{

namespace detail
{

struct sb_impl
{
  std::atomic<size_t> size;
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
  sb->size.store (0, std::memory_order_relaxed);
}

stack_node_base* stack_base::root () const
{
  return (get_impl(this->buf)->root.load (std::memory_order_relaxed));
}

void stack_base::push_node (stack_node_base *nodep)
{
  auto sb = get_impl (this->buf);

  while (true)
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

