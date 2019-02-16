STATIC_LIBS = libxrcu.a
SHARED_LIBS = libxrcu.so

HEADERS = xrcu.hpp stack.hpp hash_table.hpp skip_list.hpp   \
          xatomic.hpp lwlock.hpp optional.hpp

OBJS = xrcu.o hash_table.o stack.o lwlock.o skip_list.o
LOBJS = $(OBJS:.o=.lo)

TEST_OBJS = $(LOBJS)

-include config.mak

ALL_LIBS = $(STATIC_LIBS) $(SHARED_LIBS)

AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
CXXFLAGS += $(CXXFLAGS_AUTO)

all: $(ALL_LIBS)

check: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) tests/test.cpp $(TEST_OBJS) -o tst
	./tst

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.lo: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

libxrcu.a: $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

libxrcu.so: $(LOBJS)
	$(CXX) -fPIC -shared $(CXXFLAGS) -o $@ $(LOBJS)

install: $(ALL_LIBS)
	mkdir -p $(includedir)/xrcu
	cp libxrcu* $(libdir)/
	cp $(HEADERS) $(includedir)/xrcu

clean:
	rm -rf *.o *.lo libxrcu.* tst

