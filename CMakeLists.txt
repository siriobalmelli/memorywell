#cbuf library
add_library(cbuf SHARED cbuf.c cbuf_checkpoint.c cbuf_splice.c cbuf_int.c zcio.c)
target_compile_options(cbuf PRIVATE -fPIC -DCBUF_MEM_INTEGRITY)

link_libraries(
	cbuf
	mtsig
	sbfu
	)

add_executable(cbuf_test.exe cbuf_test.c)
add_executable(cbuf_splice_test.exe cbuf_splice_test.c)

add_test(NAME cbuf_test COMMAND cbuf_test.exe)
set_tests_properties(cbuf_test
	PROPERTIES	FAIL_REGULAR_EXPRESSION "err_cnt == [^0]"
	)

add_test(NAME cbuf_splice_integrity_test COMMAND 
	/bin/bash -c "./cbuf_splice_test.exe i"
	)
set_tests_properties(cbuf_splice_integrity_test
	PROPERTIES	FAIL_REGULAR_EXPRESSION "err_cnt == [^0]"
	)

add_test(NAME cbuf_splice_test COMMAND 
	/bin/bash -c "dd if=/dev/urandom of=./source.bin bs=$(( 50 * 1024 * 1024 )) count=1;
		echo \"cbuf as buffer for splice operations\";
	./cbuf_splice_test.exe s ./source.bin ./out.bin;
	diff ./source.bin ./out.bin;
	rm out.bin;
		echo \"cbuf with backing store\";
	./cbuf_splice_test.exe p ./source.bin ./out.bin;
	diff ./source.bin ./out.bin;
	rm out.bin;
		echo \"malloc() cbuf as buffer for splice operations\";
	./cbuf_splice_test.exe m ./source.bin ./out.bin;
	diff ./source.bin ./out.bin;
	rm *.bin"
	)
set_tests_properties(cbuf_splice_test
	PROPERTIES	FAIL_REGULAR_EXPRESSION "err_cnt == [^0]"
			FAIL_REGULAR_EXPRESSION "differ"
	)
install(TARGETS cbuf
            LIBRARY DESTINATION /usr/lib)
