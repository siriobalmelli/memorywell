.PHONY: cbuf_splice_integrity_test


cbuf_splice_integrity_test: 
	/bin/bash -c "./cbuf_splice_test.exe i"
	

.PHONY: cbuf_splice_test
	

cbuf_splice_test: 
	/bin/bash -c "dd if=/dev/urandom of=./source.bin bs=$(( 50 * 1024 * 1024 )) count=1; \
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
	
