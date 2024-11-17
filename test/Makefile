# SPDX-License-Identifier: GPL-2.0-or-later

# builds it; doesn't run it
test: test.o test_pass.o test_fail.o test_marker.o test_src_adaptor.o \
		test_basic_struct.o marker.o src_adaptor.o ../build/cf_vector.o \
		../build/cf_string.o ../build/cf_index.o ../build/cf_db.o \
		../build/db_types.o ../build/mem_db.o ../build/nop_db.o \
		../build/sql_db.o ../build/sql_query.o ../build/cf_map.o \
		../build/cf_alloc.o ../build/main_support.o
	$(LD) -o test test.o test_pass.o test_fail.o test_marker.o \
	test_src_adaptor.o test_basic_struct.o marker.o src_adaptor.o \
	../build/cf_vector.o ../build/cf_string.o ../build/cf_index.o \
	../build/cf_db.o ../build/db_types.o ../build/mem_db.o ../build/nop_db.o \
	../build/sql_db.o ../build/sql_query.o ../build/cf_map.o \
	../build/cf_alloc.o ../build/main_support.o $(SQLITE_LIB) $(CLANG_LIB)

# test cases
test_pass.o: test_pass.c test_utils.h test_runner.h ../cc_support.h
	$(CC) $(CFLAGS) -c test_pass.c -o test_pass.o
test_fail.o: test_fail.c test_utils.h test_runner.h ../cc_support.h
	$(CC) $(CFLAGS) -c test_fail.c -o test_fail.o
test_marker.o: test_marker.c marker.h ../cc_support.h test_utils.h \
		test_runner.h ../cf_alloc.h
	$(CC) $(CFLAGS) -c test_marker.c -o test_marker.o
test_src_adaptor.o: test_src_adaptor.c src_adaptor.h ../cc_support.h \
		test_utils.h test_runner.h
	$(CC) $(CFLAGS) -c test_src_adaptor.c -o test_src_adaptor.o
test_basic_struct.o: test_basic_struct.c test_utils.h ../cc_support.h \
		test_runner.h marker.h src_adaptor.h ../cf_string.h ../cf_index.h \
		../cf_db.h ../db_types.h ../cf_vector.h ../mem_db.h ../sql_db.h \
		../cf_map.h
	$(CC) $(CFLAGS) -c test_basic_struct.c -o test_basic_struct.o

# test utilities
marker.o: marker.c marker.h ../cc_support.h ../cf_assert.h ../cf_print.h \
		../cf_vector.h
	$(CC) $(CFLAGS) -c marker.c -o marker.o
src_adaptor.o: src_adaptor.c src_adaptor.h ../cc_support.h ../cf_assert.h
	$(CC) $(CFLAGS) -c src_adaptor.c -o src_adaptor.o
test.o: test.c ../main_support.h test_runner.h ../cc_support.h
	$(CC) $(CFLAGS) -c test.c -o test.o

include ../Makefile
