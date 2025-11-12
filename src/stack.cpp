/* Definitions for the stack template type.

   This file is part of xrcu.

   xrcu is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include "xrcu/stack.hpp"
#include "xrcu/xatomic.hpp"
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

static inline stack_node_base*
get_node (const stack_node_base::ptr_type& head)
{
  return (head.load (std::memory_order_relaxed));
}

stack_node_base* stack_node_base::root (const ptr_type& head)
{
  auto ret = (uintptr_t)get_node (head);
  return (((stack_node_base *)(ret & ~SPIN_BIT)));
}

void stack_node_base::push (ptr_type& head, stack_node_base *nodep)
{
  while (true)
    {
      nodep->next = get_node (head);
      if (!node_spinning_p (nodep->next) &&
          head.compare_exchange_weak (nodep->next, nodep,
                                      std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
        break;

      xatomic_spin_nop ();
    }
}

void stack_node_base::push (ptr_type& head, stack_node_base *nodep,
                            stack_node_base **outp)
{
  while (true)
    {
      auto tmp = get_node (head);
      if (!node_spinning_p (tmp))
        {
          *outp = tmp;
          if (head.compare_exchange_weak (tmp, nodep,
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed))
            break;
        }

      xatomic_spin_nop ();
    }
}

stack_node_base* stack_node_base::pop (ptr_type& head)
{
  while (true)
    {
      auto nodep = get_node (head);
      if (node_spinning_p (nodep))
        ;
      else if (!nodep)
        return (nodep);
      else if (head.compare_exchange_weak (nodep, nodep->next,
          std::memory_order_acq_rel, std::memory_order_relaxed))
        {
          nodep->next = nullptr;
          return (nodep);
        }

      xatomic_spin_nop ();
    }
}

static inline stack_node_base*
set_spin (stack_node_base::ptr_type& head)
{
  while (true)
    {
      auto tmp = get_node (head);
      if (node_spinning_p (tmp))
        continue;

      auto clr = (stack_node_base *)((uintptr_t)tmp | SPIN_BIT);
      if (head.compare_exchange_weak (tmp, clr, std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
        return (tmp);

      xatomic_spin_nop ();
    }
}

void stack_node_base::swap (ptr_type& h1, ptr_type& h2)
{
  // Prevent any further modifications.
  auto ln = set_spin (h1), rn = set_spin (h2);

  // Swap the root nodes.
  h1.store (rn, std::memory_order_release);
  h2.store (ln, std::memory_order_release);
}

stack_node_base* stack_node_base::clear (ptr_type& head)
{
  auto ret = set_spin (head);
  head.store (nullptr, std::memory_order_release);
  return (ret);
}

size_t stack_node_base::size (const ptr_type& head)
{
  auto runp = get_node (head);
  size_t ret = 0;

  for (; runp; runp = runp->next, ++ret) ;
  return (ret);
}

} } // namespaces

