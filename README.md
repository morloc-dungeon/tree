# tree

Phylogenetic trees as a recursive sum type, with the Newick format handled in
C++ and the tree statistics written as recursive morloc functions.

## What it demonstrates

The tree is one `data` declaration:

```
data RootedTree n e l = Leaf l | Node n [(e, RootedTree n e l)]
```

A tree is either a leaf or a node, and a node holds each child paired with the
edge that reaches it. The type is parameterized over what a node, an edge and a
leaf carry; the program instantiates it as `RootedTree Str Length Str`, where
`Length` is `?Real`.

Pairing each child with its edge is what a phylogeny actually needs. Newick
writes a branch length after a subtree (`(A:0.1,B:0.2)C:0.3`), and that length
belongs to the branch *above* the subtree, not to the subtree itself: `0.1` is
the length of the edge from `C` to `A`. Putting it on the edge means the model
says so. It also makes the root's case fall out correctly -- the root is
reached by no edge, so it has no length, and a length written after the root in
a file has nowhere to go and is dropped.

Three things happen at the boundary between morloc and C++:

- **C++ owns the type's native form.** `data Cpp => (RootedTree n e l) =
  "RootedTree<$1,$2,$3>" n e l` says the C++ pool should use the template
  declared in `src/newick.hpp` rather than a compiler-generated one. The
  convention is that the wrapper struct holds a `std::variant` of
  `std::shared_ptr<const T>`, one per constructor, named `RootedTree_Leaf` and
  `RootedTree_Node`, with fields `f0`, `f1`, ... in declaration order. A
  morloc tuple is a `std::tuple`, so the node's child list is a
  `std::vector<std::tuple<E, RootedTree<N, E, L>>>`. Because the C++ side
  declares the type itself, the Newick parser and printer can match on it
  directly.

- **C++ does the parsing and printing.** `read_newick` parses the text
  (nested parentheses, quoted labels, optional branch lengths) and
  `write_newick` prints it back. Neither recurses: a phylogeny can be a ladder
  thousands of levels deep, so both keep their open groups on an explicit
  stack. A tree morloc builds or reads is released without one destructor
  frame per level. Both are sourced with an `<IO>` effect since they touch
  files.

- **morloc does the recursion.** `countLeaves` and `maxDepth` are ordinary
  recursive functions over the two constructors, polymorphic in all three
  labels, and are realized in the C++ pool without any C++ being written for
  them. Each walks the children with `map (f . .1)`, where `.1` takes the
  subtree out of the `(edge, subtree)` pair; that composition compiles to a
  `std::get<1>` on a const reference, so nothing is copied to look past an
  edge.

The `read` command prints the tree as JSON, which shows the wire form of a sum
type: an argument-free constructor is its name, a constructor with fields is
`{"Ctor":[fields...]}`, and a tuple is a JSON array -- so a child reads as
`[0.1, {"Leaf":["Homo_sapiens"]}]`. `write` accepts a tree in that same form,
so a tree can be edited as JSON between a `read` and a `write`. A large tree
is passed as a file path rather than inline (`./tree read in.nwk > t.json`,
then `./tree write out.nwk t.json`), or as `-` to read it from stdin. `copy`
does both steps inside the program, so the tree never leaves the C++ pool.

## Layout

```
tree/
  README.md        # this file
  package.yaml     # package metadata
  TAGS             # group membership, one tag per line
  Makefile         # build / test / clean
  main.loc         # the type, the CLI, and the two recursive functions
  src/
    newick.hpp     # the C++ template for the type, and the Newick reader/writer
  test/
    primates.nwk   # a small labelled tree with branch lengths
    bare.nwk       # a tree with neither internal labels nor branch lengths
    rooted.nwk     # a length written after the root, which has no edge
    unclosed.nwk   # malformed input, expected to be rejected
    exp.txt        # expected output of `make test`
```

## Usage

```
$ ./tree leaves test/primates.nwk
4
$ ./tree depth test/primates.nwk
3
$ ./tree read test/bare.nwk
{"Node":["",[[null,{"Leaf":["A"]}],[null,{"Node":["",[[null,{"Leaf":["B"]}], ...
$ ./tree copy test/primates.nwk copy.nwk
$ diff test/primates.nwk copy.nwk && echo identical
identical
```

`depth` counts edges on the longest root-to-leaf path, so a lone leaf has
depth 0. A malformed file is reported with its offset and a nonzero exit. A
length written after the root is dropped, since no edge reaches the root:
`(A:1,B:2)root:9;` is written back as `(A:1,B:2)root;`.

## Build and test

```
make build    # morloc make -o tree main.loc
make test     # runs the commands above and diffs against test/exp.txt
make clean    # removes the executable, the build directory, and test output
```
