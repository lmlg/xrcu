#include "skip_list.hpp"
#include <new>

namespace xrcu
{

namespace detail
{

sl_node_base* sl_node_base::alloc (unsigned int lvl, size_t size)
{
  void *p = ::operator new (size + lvl * sizeof (uintptr_t));
  return (new (p) sl_node_base (lvl, (uintptr_t)((char *)p + size)));
}

void sl_node_base::dealloc (void *ptr)
{
  ::operator delete (ptr);
}

} // namespace detail

} // namespace xrcu
