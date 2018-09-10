STATIC_LIBS = libxrcu.a
SHARED_LIBS = libxrcu.so

-include config.mak

OBJS = xrcu.o hash_table.o stack.o
ALL_LIBS = $(STATIC_LIBS) $(SHARED_LIBS)
HEADERS = xrcu.hpp stack.hpp hash_table.hpp

AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
LOBJS = $(OBJS:.o=.lo)
CXXFLAGS += $(CXXFLAGS_AUTO)

all: $(ALL_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.lo: %.cpp
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

libxrcu.a: $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

libxrcu.so: $(LOBJS)
	$(CXX) -fPIC -shared $(CXXFLAGS) -o $@ $(LOBJS)

install: $(ALL_LIBS)
	mkdir -p $(libdir)/xrcu $(includedir)/xrcu
	cp libxrcu.* $(libdir)/xrcu
	cp $(HEADERS) $(includedir)/xrcu

clean:
	rm -rf *.o *.lo libxrcu.*

