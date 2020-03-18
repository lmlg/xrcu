ifeq ($(OS), Windows_NT)
	PIC_FLAG =
	STATIC_EXT = lib
	DYNAMIC_EXT = dll
else
	PIC_FLAG = -fPIC
	STATIC_EXT = a
	DYNAMIC_EXT = so
endif

STATIC_LIBS = libxrcu.$(STATIC_EXT)
SHARED_LIBS = libxrcu.$(DYNAMIC_EXT)

HEADERS = xrcu.hpp stack.hpp hash_table.hpp skip_list.hpp   \
          xatomic.hpp lwlock.hpp optional.hpp queue.hpp

OBJS = xrcu.o hash_table.o stack.o lwlock.o skip_list.o queue.o utils.o
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
	$(CXX) $(CXXFLAGS) $(PIC_FLAG) -c $< -o $@

libxrcu.$(STATIC_EXT): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

libxrcu.$(DYNAMIC_EXT): $(LOBJS)
	$(CXX) -fPIC -shared $(CXXFLAGS) -o $@ $(LOBJS)

install: $(ALL_LIBS)
	mkdir -p $(includedir)/xrcu
	cp libxrcu* $(libdir)/
	cp $(HEADERS) $(includedir)/xrcu

clean:
	rm -rf *.o *.lo libxrcu.* tst

