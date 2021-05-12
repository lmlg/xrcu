# XRCU
This library implements efficient lockless synchronization for
read-mostly tasks. In addition, it provides several data lockless
data structures that make use of said synchronization, with an API
that closely resembles the one used in C++'s STL. These data structures
include: stacks, queues (multiple producer, multiple consumer),
skip lists and hash tables.

## Installation
As with most GNU packages, xrcu follows a very simple convention:
```shell
./configure
make
make install
```

## Usage
Even though the data structures are generic and thus implemented almost
completely in single headers, there's still the need to link with this
library (-lxrcu) to pull the runtime.

## Documentation
See docs/xrcu.html for a full overview of the design and its API.

## Authors
Luciano Lo Giudice

Agustina Arzille

## License
Licensed under the GPLv3.
