CXX = g++
PKG_CONFIG = pkg-config
CXXFLAGS = -Iinclude $(shell $(PKG_CONFIG) --cflags libndn-cxx PSync)
LDFLAGS = $(shell $(PKG_CONFIG) --libs libndn-cxx PSync)

TARGETS = psync-start psync-update

all: $(TARGETS)

psync-start: psync-start.cpp
	$(CXX) -o $@ $< $(CXXFLAGS) $(LDFLAGS)

psync-update: psync-update.cpp
	$(CXX) -o $@ $< $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGETS)
