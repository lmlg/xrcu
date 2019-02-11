#include <iostream>
#include <cstdlib>

#define ASSERT(Cond)   \
  do   \
    {   \
      if (!(Cond))   \
        {   \
          std::cerr << "\n\nAssertion failed: " << #Cond <<   \
            "\nLine: " << __LINE__ << "\nFile: " << __FILE__ << "\n\n";   \
          exit (EXIT_FAILURE);   \
        }   \
    }   \
  while (0)

#define TEST(Type, Fn)   \
  do   \
    {   \
      std::cout << "Testing " << Type << "...";   \
      Fn ();   \
      std::cout << " OK\n";   \
    }   \
  while (0)
