#include "skip_list.hpp"
#include <new>

namespace xrcu
{

namespace detail
{

void* sl_alloc_node (unsigned int lvl, size_t size, uintptr_t **outpp)
{
  void *p = ::operator new (size + lvl * sizeof (uintptr_t));
  *outpp = (uintptr_t *)((char *)p + size);
  return (p);
}

void sl_dealloc_node (void *ptr)
{
  ::operator delete (ptr);
}

} // namespace detail

} // namespace xrcu
