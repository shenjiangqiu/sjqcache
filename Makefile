CPPFLAGS := -fPIC -O3 -Wall -g -std=c++17
libsjqcache.so:cache.o
	g++ --shared $^ -o $@
test:test.out
cache_test.o:cache_test.cc  cache.h 
	g++ $(CPPFLAGS) -c -o $@ $<; 
cache.o: cache.cc cache.h
	g++ $(CPPFLAGS) -c -o $@ $<; 
test.out:cache_test.o
	g++ $^ -o $@ -lsjqcache
.PHONY:clean
clean:
	rm -rf *.o *.out

install:
	install -m755 -D  ./cache.h ${DSTDIR}/usr/include/cache.h 
	install -m755 -D ./libsjqcache.so ${DSTDIR}/usr/lib/libsjqcache.so 
