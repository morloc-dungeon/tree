# Build/test conventions for a morloc-dungeon example.
#
# dungeon-master drives exactly these three targets:
#
#   make build   build the program; nonzero exit on any build failure
#   make test    run it and diff against test/exp.txt; nonzero exit on mismatch
#   make clean   remove build products
#
# `test` depends on `build`, so `make test` builds and checks in one step.

EXE := tree
SRC := main.loc

.PHONY: build test clean

build:
	morloc make -o $(EXE) $(SRC)

test: build
	: > test/obs.txt
	./$(EXE) read test/primates.nwk >> test/obs.txt
	./$(EXE) leaves test/primates.nwk >> test/obs.txt
	./$(EXE) depth test/primates.nwk >> test/obs.txt
	./$(EXE) write test/copy.nwk "$$(./$(EXE) read test/primates.nwk)"
	cat test/copy.nwk >> test/obs.txt
	./$(EXE) read test/bare.nwk >> test/obs.txt
	./$(EXE) leaves test/bare.nwk >> test/obs.txt
	./$(EXE) depth test/bare.nwk >> test/obs.txt
	./$(EXE) write test/copy.nwk "$$(./$(EXE) read test/bare.nwk)"
	cat test/copy.nwk >> test/obs.txt
	./$(EXE) write test/copy.nwk '{"Leaf":[{"name":"one taxon","branch":1.5}]}'
	cat test/copy.nwk >> test/obs.txt
	./$(EXE) leaves test/copy.nwk >> test/obs.txt
	./$(EXE) depth test/copy.nwk >> test/obs.txt
	if ./$(EXE) leaves test/unclosed.nwk >> test/obs.txt 2>/dev/null; then echo UNEXPECTED >> test/obs.txt; else echo OK >> test/obs.txt; fi
	diff -u test/exp.txt test/obs.txt

clean:
	rm -rf $(EXE) *-build test/obs.txt test/copy.nwk
