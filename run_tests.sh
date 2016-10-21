#!/bin/bash

if ! ./cbuf_splice_test.exe i; then
	exit $?
fi

dd if=/dev/urandom of=./source.bin bs=$(( 50 * 1024 * 1024 )) count=1


diff_src()
{
	if ! diff ./source.bin ./out.bin; then
		exit $?
	fi
	rm -f out.bin
}

echo "cbuf as buffer for splice operations"
if ! ./cbuf_splice_test.exe s ./source.bin ./out.bin; then
	exit $?
fi
diff_src

echo "cbuf with backing store"
if ! ./cbuf_splice_test.exe p ./source.bin ./out.bin; then
	exit $?
fi
diff_src

echo "malloc() cbuf as buffer for splice operations"
if ! ./cbuf_splice_test.exe m ./source.bin ./out.bin; then
	exit $?
fi
diff_src

# clean up and exit
rm -f source.bin
