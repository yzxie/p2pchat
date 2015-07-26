#! /bin/sh

TEMPFILE=./tmp

while test $# -gt 0
do
	sed 's/\t//1' $1 > $TEMPFILE
	cp $TEMPFILE $1
	rm $TEMPFILE
	shift
done