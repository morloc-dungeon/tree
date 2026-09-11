# tree

Phylogenetic trees as a recursive sum type, with the Newick format handled in
C++ and the tree statistics written as recursive morloc functions.

## What it demonstrates

The tree is one `data` declaration:

```
data RootedTree n e l = Leaf l | Node n [RootedTree n e l]
```

A tree is either a leaf or a node with a list of subtrees, and the type is
parameterized over what a node, an edge and a leaf carry. The program
instantiates it as `RootedTree Label Real Label`, where a `Label` is a name and
an optional branch length, which is exactly what Newick writes beside a node.
(The edge parameter `e` is not used by either constructor here; it is part of
the general signature so that a labelling that puts data on edges rather than
nodes can reuse the same type.)

Three things happen at the boundary between morloc and C++:

- **C++ owns the type's native form.** `data Cpp => (RootedTree n e l) =
  "RootedTree<$1,$2,$3>" n e l` says the C++ pool should use the template
  declared in `src/newick.hpp` rather than a compiler-generated one. The
  convention is that the wrapper struct holds a `std::variant` of
  `shared_ptr`s to one struct per constructor, named `RootedTree_Leaf` and
  `RootedTree_Node`, with fields `f0`, `f1`, ... in declaration order. Because
  the C++ side declares the type itself, the Newick parser and printer can
  match on it directly.

- **C++ does the parsing and printing.** `read_newick` is a small
  recursive-descent parser (nested parentheses, quoted labels, optional
  branch lengths) that builds the tree; `write_newick` walks it back out.
  Both are sourced with an `<IO>` effect since they touch files.

- **morloc does the recursion.** `countLeaves` and `maxDepth` are ordinary
  recursive functions over the two constructors, polymorphic in all three
  labels, and are realized in the C++ pool without any C++ being written for
  them.

The `read` command prints the tree as JSON, which shows the wire form of a sum
type: an argument-free constructor is its name, a constructor with fields is
`{"Ctor":[fields...]}`. `write` accepts a tree in that same form, so a tree can
be edited as JSON between a `read` and a `write`.

## Layout

```
tree/
  README.md        # this file
  package.yaml     # package metadata
  TAGS             # group membership, one tag per line
  Makefile         # build / test / clean
  src/
    main.loc       # the type, the CLI, and the two recursive functions
    newick.hpp     # the C++ template for the type, and the Newick reader/writer
  test/
    primates.nwk   # a small labelled tree with branch lengths
    bare.nwk       # a tree with neither internal labels nor branch lengths
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
{"Node":[{"name":"","branch":null},[{"Leaf":[{"name":"A","branch":null}]}, ...
$ ./tree write copy.nwk "$(./tree read test/primates.nwk)"
$ diff test/primates.nwk copy.nwk && echo identical
identical
```

`depth` counts edges on the longest root-to-leaf path, so a lone leaf has
depth 0. A malformed file is reported with its offset and a nonzero exit.

## Build and test

```
make build    # morloc make -o tree src/main.loc
make test     # runs the commands above and diffs against test/exp.txt
make clean    # removes the executable, the build directory, and test output
```
