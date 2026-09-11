#pragma once
#include "mlccpptypes/prelude.hpp"
#include <cctype>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// The morloc type `RootedTree n e l` maps to this template. Each arm is a
// struct named after the type with `_<Constructor>` appended, its fields
// positional as `f0`, `f1`, ...; the variant holds every arm behind a
// shared_ptr so the recursive `Node` arm has a finite size.
template <typename N, typename E, typename L> struct RootedTree_Leaf;
template <typename N, typename E, typename L> struct RootedTree_Node;

template <typename N, typename E, typename L> struct RootedTree {
    std::variant<std::shared_ptr<RootedTree_Leaf<N, E, L>>,
                 std::shared_ptr<RootedTree_Node<N, E, L>>> v;
};

template <typename N, typename E, typename L> struct RootedTree_Leaf { L f0; };
template <typename N, typename E, typename L> struct RootedTree_Node {
    N f0;
    std::vector<RootedTree<N, E, L>> f1;
};

struct Label {
    std::string name;
    std::optional<double> branch;
};

using Phylo = RootedTree<Label, double, Label>;

// ---------------------------------------------------------------- parsing

class NewickParser {
  public:
    explicit NewickParser(const std::string& text) : s(text), i(0) {}

    Phylo parse() {
        Phylo t = subtree();
        skip();
        if (i >= s.size() || s[i] != ';')
            fail("expected ';' at end of tree");
        return t;
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

    Phylo subtree() {
        skip();
        if (at('(')) {
            i++;
            std::vector<Phylo> kids;
            kids.push_back(subtree());
            skip();
            while (at(',')) {
                i++;
                kids.push_back(subtree());
                skip();
            }
            if (!at(')')) fail("expected ')' or ','");
            i++;
            auto node = std::make_shared<RootedTree_Node<Label, double, Label>>();
            node->f0 = label();
            node->f1 = std::move(kids);
            return Phylo{node};
        }
        auto leaf = std::make_shared<RootedTree_Leaf<Label, double, Label>>();
        leaf->f0 = label();
        return Phylo{leaf};
    }

    // A name, optionally quoted, followed by an optional ":length".
    Label label() {
        skip();
        Label lab{"", std::nullopt};
        if (at('\'')) {
            i++;
            while (true) {
                if (i >= s.size()) fail("unterminated quoted label");
                if (s[i] == '\'') {
                    if (i + 1 < s.size() && s[i + 1] == '\'') { lab.name += '\''; i += 2; continue; }
                    i++;
                    break;
                }
                lab.name += s[i++];
            }
        } else {
            while (i < s.size() && std::string("(),:;").find(s[i]) == std::string::npos
                   && !std::isspace(static_cast<unsigned char>(s[i])))
                lab.name += s[i++];
        }
        skip();
        if (at(':')) {
            i++;
            skip();
            size_t start = i;
            while (i < s.size() && std::string("(),:;").find(s[i]) == std::string::npos
                   && !std::isspace(static_cast<unsigned char>(s[i])))
                i++;
            if (start == i) fail("expected branch length after ':'");
            try {
                lab.branch = std::stod(s.substr(start, i - start));
            } catch (const std::exception&) {
                fail("bad branch length '" + s.substr(start, i - start) + "'");
            }
        }
        return lab;
    }
};

// --------------------------------------------------------------- printing

inline std::string format_branch(const std::optional<double>& x) {
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

inline void show_newick(const Phylo& t, std::string& out) {
    if (auto p = std::get_if<std::shared_ptr<RootedTree_Node<Label, double, Label>>>(&t.v)) {
        out += '(';
        for (size_t k = 0; k < (*p)->f1.size(); k++) {
            if (k > 0) out += ',';
            show_newick((*p)->f1[k], out);
        }
        out += ')';
        out += quote_name((*p)->f0.name) + format_branch((*p)->f0.branch);
    } else {
        const Label& lab = std::get<std::shared_ptr<RootedTree_Leaf<Label, double, Label>>>(t.v)->f0;
        out += quote_name(lab.name) + format_branch(lab.branch);
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
    std::string out;
    show_newick(t, out);
    out += ";\n";
    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot open " + path);
    f << out;
    return mlc::Unit();
}
