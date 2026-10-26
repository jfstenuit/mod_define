# Build and test mod_define with apxs (Debian: apache2-dev)

APXS ?= apxs

all: .libs/mod_define.so

.libs/mod_define.so: mod_define.c
	$(APXS) -c -Wc,-Wall mod_define.c

test: all
	tests/run-tests.sh .libs/mod_define.so

clean:
	rm -f mod_define.la mod_define.lo mod_define.slo mod_define.o
	rm -f .libs/mod_define.*
	-rmdir .libs

.PHONY: all test clean
