#include "utils.hpp"
#include <new>

namespace xrcu
{

namespace detail
{

void* alloc_wrapped (size_t size)
{
  return (::operator new (size));
}

void dealloc_wrapped (void *ptr)
{
  ::operator delete (ptr);
}

} // namespace detail

} // namespace xrcu
