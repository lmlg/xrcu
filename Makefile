STATIC_LIBS = libxrcu.a
SHARED_LIBS = libxrcu.so

-include config.mak

OBJS = xrcu.o hash_table.o stack.o lwlock.o skip_list.o
ALL_LIBS = $(STATIC_LIBS) $(SHARED_LIBS)
HEADERS = xrcu.hpp stack.hpp hash_table.hpp skip_list.hpp   \
          xatomic.hpp lwlock.hpp optional.hpp

AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
LOBJS = $(OBJS:.o=.lo)
CXXFLAGS += $(CXXFLAGS_AUTO)

all: $(ALL_LIBS)

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
	mkdir -p $(libdir)/xrcu $(includedir)/xrcu
	cp libxrcu* $(libdir)/xrcu
	cp $(HEADERS) $(includedir)/xrcu

clean:
	rm -rf *.o *.lo libxrcu.*

