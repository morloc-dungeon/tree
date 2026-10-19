#pragma once
#include "mlc_rec.hpp"
#include "mlccpptypes/prelude.hpp"
#include <cctype>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

// The morloc type `RootedTree n e l` maps to this template. Each arm is a
// struct named after the type with `_<Constructor>` appended, its fields
// positional as `f0`, `f1`, ...; the variant holds every arm behind a
// std::shared_ptr<const T>, which gives the recursive `Node` arm a finite
// size and lets copies share an arm that nothing changes after it is built.
template <typename N, typename E, typename L> struct RootedTree_Leaf;
template <typename N, typename E, typename L> struct RootedTree_Node;

template <typename N, typename E, typename L> struct RootedTree {
    std::variant<std::shared_ptr<const RootedTree_Leaf<N, E, L>>,
                 std::shared_ptr<const RootedTree_Node<N, E, L>>> v;
};

template <typename N, typename E, typename L> struct RootedTree_Leaf { L f0; };

// A child and the edge reaching it travel together, so the node's field is
// a vector of pairs rather than a vector of subtrees.
template <typename N, typename E, typename L> struct RootedTree_Node {
    N f0;
    std::vector<std::tuple<E, RootedTree<N, E, L>>> f1;
};

// Newick names both tips and internal nodes, and puts lengths on branches.
using Length = std::optional<double>;
using Phylo = RootedTree<std::string, Length, std::string>;
using PhyloLeaf = RootedTree_Leaf<std::string, Length, std::string>;
using PhyloNode = RootedTree_Node<std::string, Length, std::string>;
using PhyloEdge = std::tuple<Length, Phylo>;

// ---------------------------------------------------------------- parsing

// Newick nests arbitrarily, and a real phylogeny can be a ladder thousands
// of levels deep, so neither the parser nor the printer recurses: each
// keeps the open groups on an explicit stack and runs in constant stack
// space.
class NewickParser {
  public:
    explicit NewickParser(const std::string& text) : s(text), i(0) {}

    Phylo parse() {
        std::vector<std::vector<PhyloEdge>> open;
        while (true) {
            skip();
            if (at('(')) {
                i++;
                open.emplace_back();
                continue;
            }
            // A subtree that is not a group is a leaf. Whatever length
            // follows it labels the edge above it.
            Phylo t{std::make_shared<const PhyloLeaf>(PhyloLeaf{name()})};
            Length e = length();
            // Hand the finished subtree upward: a ',' opens a sibling, a ')'
            // closes the group into a node, which is itself a finished
            // subtree; at the top level only ';' may follow.
            while (true) {
                skip();
                if (open.empty()) {
                    if (!at(';')) fail("expected ';' at end of tree");
                    // A length on the root labels no edge, and is dropped.
                    return t;
                }
                if (at(',')) {
                    i++;
                    open.back().emplace_back(std::move(e), std::move(t));
                    break;
                }
                if (!at(')')) fail("expected ')' or ','");
                i++;
                open.back().emplace_back(std::move(e), std::move(t));
                auto node = std::make_shared<PhyloNode>();
                node->f1 = std::move(open.back());
                open.pop_back();
                node->f0 = name();
                e = length();
                t = Phylo{node};
            }
        }
    }

  private:
    const std::string& s;
    size_t i;

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("newick: " + msg + " at offset " + std::to_string(i));
    }

    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    }

    bool at(char c) { return i < s.size() && s[i] == c; }

    bool atDelim() {
        return i >= s.size()
            || std::string("(),:;").find(s[i]) != std::string::npos
            || std::isspace(static_cast<unsigned char>(s[i]));
    }

    // A name, optionally quoted. An unnamed node has the empty name.
    std::string name() {
        skip();
        std::string out;
        if (at('\'')) {
            i++;
            while (true) {
                if (i >= s.size()) fail("unterminated quoted name");
                if (s[i] == '\'') {
                    if (i + 1 < s.size() && s[i + 1] == '\'') { out += '\''; i += 2; continue; }
                    i++;
                    break;
                }
                out += s[i++];
            }
        } else {
            while (!atDelim()) out += s[i++];
        }
        return out;
    }

    // The ":length" that follows a subtree, if it is given.
    Length length() {
        skip();
        if (!at(':')) return std::nullopt;
        i++;
        skip();
        size_t start = i;
        while (!atDelim()) i++;
        if (start == i) fail("expected branch length after ':'");
        try {
            return std::stod(s.substr(start, i - start));
        } catch (const std::exception&) {
            fail("bad branch length '" + s.substr(start, i - start) + "'");
        }
    }
};

// --------------------------------------------------------------- printing

inline std::string format_branch(const Length& x) {
    if (!x) return "";
    std::ostringstream o;
    o.precision(15);
    o << ':' << *x;
    return o.str();
}

inline std::string quote_name(const std::string& name) {
    bool plain = !name.empty();
    for (char c : name)
        if (std::string("(),:;' \t\n").find(c) != std::string::npos) plain = false;
    if (plain) return name;
    if (name.empty()) return "";
    std::string q = "'";
    for (char c : name) q += (c == '\'') ? "''" : std::string(1, c);
    return q + "'";
}

inline std::string show_newick(const Phylo& root) {
    std::string out;
    // An open node, the index of the child being written, and the edge
    // that reaches the node itself.
    struct Frame { const PhyloNode* node; size_t k; const Length* edge; };
    std::vector<Frame> open;
    const Phylo* t = &root;
    const Length* edge = nullptr;  // the root is reached by no edge
    while (true) {
        if (auto p = std::get_if<std::shared_ptr<const PhyloNode>>(&t->v)) {
            out += '(';
            open.push_back(Frame{p->get(), 0, edge});
        } else {
            out += quote_name(std::get<std::shared_ptr<const PhyloLeaf>>(t->v)->f0);
            if (edge) out += format_branch(*edge);
        }
        // Advance to the next unwritten child, closing every node whose
        // children are done.
        while (true) {
            if (open.empty()) return out;
            Frame& f = open.back();
            if (f.k < f.node->f1.size()) {
                if (f.k > 0) out += ',';
                const PhyloEdge& child = f.node->f1[f.k++];
                edge = &std::get<0>(child);
                t = &std::get<1>(child);
                break;
            }
            out += ')';
            out += quote_name(f.node->f0);
            if (f.edge) out += format_branch(*f.edge);
            open.pop_back();
        }
    }
}

// ------------------------------------------------------------------- io

inline Phylo read_newick(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot open " + path);
    std::stringstream buf;
    buf << f.rdbuf();
    std::string text = buf.str();
    return NewickParser(text).parse();
}

inline mlc::Unit write_newick(const std::string& path, const Phylo& t) {
    std::string out = show_newick(t) + ";\n";
    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot open " + path);
    f << out;
    return mlc::Unit();
}
