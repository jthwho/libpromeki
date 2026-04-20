/**
 * @file      variantquery.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <promeki/variantquery.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// ============================================================
// Token stream
// ============================================================

enum class Tok {
        End,
        LParen,
        RParen,
        Bang,
        AndAnd,
        OrOr,
        Eq,
        NotEq,
        Lt,
        LtEq,
        Gt,
        GtEq,
        Tilde,            // ~
        TildeTilde,       // ~~
        IntLit,
        FloatLit,
        StringLit,
        RegexLit,
        BoolLit,
        Has,              // the 'has' keyword
        Ident,            // any identifier path, potentially multi-segment
};

struct Token {
        Tok    kind{Tok::End};
        String text;      // identifier / literal source (no quotes for strings)
        int    column{0}; // 1-based, for diagnostics
        double fval{0.0}; // filled for IntLit/FloatLit
        bool   bval{false};
};

// ============================================================
// Lexer
// ============================================================

class Lexer {
        public:
                Lexer(const char *src, size_t len) : _src(src), _len(len) {}

                // Reads the next token; on error sets _err and returns End.
                Token next() {
                        skipWhitespace();
                        if(_pos >= _len) return make(Tok::End);
                        const int startCol = static_cast<int>(_pos + 1);
                        char c = _src[_pos];

                        // Punctuation / operators
                        if(c == '(') { ++_pos; return make(Tok::LParen, startCol); }
                        if(c == ')') { ++_pos; return make(Tok::RParen, startCol); }
                        if(c == '!') {
                                if(peek(1) == '=') { _pos += 2; return make(Tok::NotEq, startCol); }
                                ++_pos; return make(Tok::Bang, startCol);
                        }
                        if(c == '&' && peek(1) == '&') { _pos += 2; return make(Tok::AndAnd, startCol); }
                        if(c == '|' && peek(1) == '|') { _pos += 2; return make(Tok::OrOr, startCol); }
                        if(c == '=' && peek(1) == '=') { _pos += 2; return make(Tok::Eq, startCol); }
                        if(c == '<') {
                                if(peek(1) == '=') { _pos += 2; return make(Tok::LtEq, startCol); }
                                ++_pos; return make(Tok::Lt, startCol);
                        }
                        if(c == '>') {
                                if(peek(1) == '=') { _pos += 2; return make(Tok::GtEq, startCol); }
                                ++_pos; return make(Tok::Gt, startCol);
                        }
                        if(c == '~') {
                                if(peek(1) == '~') { _pos += 2; return make(Tok::TildeTilde, startCol); }
                                ++_pos; return make(Tok::Tilde, startCol);
                        }

                        // String literal
                        if(c == '"') return readString(startCol);
                        // Regex literal
                        if(c == '/') return readRegex(startCol);
                        // Numeric literal (allow leading minus)
                        if(isDigit(c) || (c == '-' && isDigit(peek(1)))) return readNumber(startCol);

                        // Identifier or keyword ('has', 'true', 'false', plain identifier
                        // paths like 'Image[0].Meta.Timecode').
                        if(isIdStart(c)) return readIdent(startCol);

                        _err = String::sprintf("unexpected character '%c' at col %d", c, startCol);
                        _pos = _len;
                        return make(Tok::End, startCol);
                }

                const String &error() const { return _err; }

        private:
                const char *_src;
                size_t      _len;
                size_t      _pos{0};
                String      _err;

                void skipWhitespace() {
                        while(_pos < _len && std::isspace(static_cast<unsigned char>(_src[_pos]))) ++_pos;
                }
                char peek(size_t off) const {
                        return _pos + off < _len ? _src[_pos + off] : '\0';
                }
                static bool isDigit(char c) { return c >= '0' && c <= '9'; }
                static bool isIdStart(char c) {
                        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
                }
                static bool isIdBody(char c) {
                        // Identifier path continuation: letters, digits, underscore,
                        // and the subscript / member-access punctuation so
                        // "Image[0].Meta.Width" is one token.
                        return isIdStart(c) || (c >= '0' && c <= '9')
                               || c == '.' || c == '[' || c == ']';
                }

                Token make(Tok k, int col = 0) {
                        Token t;
                        t.kind = k;
                        t.column = col;
                        return t;
                }

                Token readString(int startCol) {
                        Token t;
                        t.kind   = Tok::StringLit;
                        t.column = startCol;
                        ++_pos; // consume opening quote
                        std::string out;
                        while(_pos < _len && _src[_pos] != '"') {
                                char c = _src[_pos];
                                if(c == '\\' && _pos + 1 < _len) {
                                        char n = _src[_pos + 1];
                                        switch(n) {
                                        case 'n':  out.push_back('\n'); _pos += 2; continue;
                                        case 't':  out.push_back('\t'); _pos += 2; continue;
                                        case '\\': out.push_back('\\'); _pos += 2; continue;
                                        case '"':  out.push_back('"');  _pos += 2; continue;
                                        default:   out.push_back(n);    _pos += 2; continue;
                                        }
                                }
                                out.push_back(c);
                                ++_pos;
                        }
                        if(_pos >= _len) {
                                _err = String::sprintf("unterminated string literal starting at col %d", startCol);
                                _pos = _len;
                                return make(Tok::End, startCol);
                        }
                        ++_pos; // consume closing quote
                        t.text = String(std::move(out));
                        return t;
                }

                Token readRegex(int startCol) {
                        Token t;
                        t.kind   = Tok::RegexLit;
                        t.column = startCol;
                        ++_pos; // consume opening '/'
                        std::string out;
                        while(_pos < _len && _src[_pos] != '/') {
                                char c = _src[_pos];
                                if(c == '\\' && _pos + 1 < _len) {
                                        // Preserve the escape so std::regex sees it.
                                        out.push_back(c);
                                        out.push_back(_src[_pos + 1]);
                                        _pos += 2;
                                        continue;
                                }
                                out.push_back(c);
                                ++_pos;
                        }
                        if(_pos >= _len) {
                                _err = String::sprintf("unterminated regex literal starting at col %d", startCol);
                                _pos = _len;
                                return make(Tok::End, startCol);
                        }
                        ++_pos; // consume closing '/'
                        t.text = String(std::move(out));
                        return t;
                }

                Token readNumber(int startCol) {
                        Token t;
                        t.column = startCol;
                        const size_t start = _pos;
                        if(_src[_pos] == '-') ++_pos;
                        while(_pos < _len && isDigit(_src[_pos])) ++_pos;
                        bool isFloat = false;
                        if(_pos < _len && _src[_pos] == '.') {
                                isFloat = true;
                                ++_pos;
                                while(_pos < _len && isDigit(_src[_pos])) ++_pos;
                        }
                        if(_pos < _len && (_src[_pos] == 'e' || _src[_pos] == 'E')) {
                                isFloat = true;
                                ++_pos;
                                if(_pos < _len && (_src[_pos] == '+' || _src[_pos] == '-')) ++_pos;
                                while(_pos < _len && isDigit(_src[_pos])) ++_pos;
                        }
                        t.text = String(_src + start, _pos - start);
                        t.kind = isFloat ? Tok::FloatLit : Tok::IntLit;
                        char *endp = nullptr;
                        t.fval = std::strtod(t.text.cstr(), &endp);
                        return t;
                }

                Token readIdent(int startCol) {
                        Token t;
                        t.column = startCol;
                        const size_t start = _pos;
                        while(_pos < _len && isIdBody(_src[_pos])) ++_pos;
                        t.text = String(_src + start, _pos - start);

                        if(t.text == String("true"))  { t.kind = Tok::BoolLit; t.bval = true;  return t; }
                        if(t.text == String("false")) { t.kind = Tok::BoolLit; t.bval = false; return t; }
                        if(t.text == String("has"))   { t.kind = Tok::Has;                     return t; }
                        t.kind = Tok::Ident;
                        return t;
                }
};

// ============================================================
// Operand / coercion
// ============================================================

// Operand: either a literal value, a regex, or a key (resolved per-match).
struct Operand {
        enum Kind { IsKey, IsLiteral, IsRegex };
        Kind    kind{IsLiteral};
        String  key;          // only when kind == IsKey
        Variant literal;      // only when kind == IsLiteral
        std::shared_ptr<std::regex> regex;  // only when kind == IsRegex
};

// Coerce a string literal to match the spec-declared type of the key
// it is being compared against.  Falls back to the literal unchanged
// when the Context cannot provide a spec.
//
// @p context carries the live value of the key the literal is being
// compared against; it lets us adopt information the literal cannot
// supply on its own.  For example, @c Timecode::fromString("01:00:00:00")
// produces a mode-less Timecode; comparing that to a moded Timecode
// would fail on the mode mismatch.  When @p context holds a valid
// Timecode we promote the parsed value to the same mode so typed
// comparisons work as the caller expects.  This mode promotion runs
// whether or not a spec was found, because the Timecode parse path
// inside Variant's own cross-type conversion has the same limitation.
static Variant coerceLiteralToKey(const Variant &literal, const String &keyPath,
                                  const Variant &context,
                                  const detail::VariantQueryContext &ctx) {
        if(literal.type() != Variant::TypeString) return literal;

        Variant parsed = literal;
        bool parsedViaSpec = false;
        if(ctx.specFor) {
                const VariantSpec *sp = ctx.specFor(keyPath);
                if(sp != nullptr && !sp->acceptsType(Variant::TypeString)) {
                        Error pe;
                        Variant candidate = sp->parseString(literal.get<String>(), &pe);
                        if(pe.isOk()) {
                                parsed = candidate;
                                parsedViaSpec = true;
                        }
                }
        }

        // Timecode promotion: a string parsed to a Timecode (either via
        // spec or via Variant's own cross-type conversion) carries only
        // the digits — mode absent or default — which makes
        // Timecode::operator== reject a comparison against a moded
        // Timecode and makes ordering fall through to string compare.
        // When context is a moded Timecode, adopt its mode on the
        // parsed value so comparisons go through the digit-accurate
        // toFrameNumber path.
        auto promoteTimecode = [&context](Variant v) -> Variant {
                if(v.type() != Variant::TypeTimecode
                   || context.type() != Variant::TypeTimecode) return v;
                Timecode tc  = v.get<Timecode>();
                Timecode ref = context.get<Timecode>();
                if(ref.isValid() && tc.mode() != ref.mode()) {
                        tc.setMode(ref.mode());
                        return Variant(tc);
                }
                return v;
        };

        if(parsedViaSpec) return promoteTimecode(parsed);

        // Spec unavailable or rejected the literal: for Timecode
        // keys we still want mode-promoted comparison, so parse the
        // string through @ref Timecode::fromString directly and
        // adopt the context's mode.  Other typed comparisons fall
        // through to @ref Variant::operator== which already performs
        // cross-type conversion (e.g. PixelDesc from String via its
        // own string constructor).
        if(context.type() == Variant::TypeTimecode) {
                auto [tc, pe] = Timecode::fromString(literal.get<String>());
                if(pe.isOk()) return promoteTimecode(Variant(tc));
        }
        return literal;
}

enum class Ord { Less, Equal, Greater, Incomparable };

static Ord orderVariants(const Variant &a, const Variant &b) {
        // Numeric double path.
        Error err;
        double da = a.get<double>(&err);
        if(err.isOk()) {
                double db = b.get<double>(&err);
                if(err.isOk()) {
                        if(da < db) return Ord::Less;
                        if(da > db) return Ord::Greater;
                        return Ord::Equal;
                }
        }
        // Timecode path — if either side is a Timecode, try both-as-Timecode.
        if(a.type() == Variant::TypeTimecode || b.type() == Variant::TypeTimecode) {
                err = Error::Ok;
                Timecode ta = a.get<Timecode>(&err);
                if(err.isOk() && ta.isValid()) {
                        err = Error::Ok;
                        Timecode tb = b.get<Timecode>(&err);
                        if(err.isOk() && tb.isValid()) {
                                if(ta < tb) return Ord::Less;
                                if(ta > tb) return Ord::Greater;
                                return Ord::Equal;
                        }
                }
        }
        // String fallback.
        String sa = a.get<String>();
        String sb = b.get<String>();
        if(sa < sb) return Ord::Less;
        if(sa > sb) return Ord::Greater;
        return Ord::Equal;
}

} // namespace

// ============================================================
// Node hierarchy (private to VariantQuery)
// ============================================================

namespace detail {

class VariantQueryNode {
        public:
                virtual ~VariantQueryNode() = default;
                virtual bool eval(const VariantQueryContext &ctx) const = 0;
};

} // namespace detail

namespace {

class OrNode : public detail::VariantQueryNode {
        public:
                OrNode(std::unique_ptr<detail::VariantQueryNode> l,
                       std::unique_ptr<detail::VariantQueryNode> r) : lhs(std::move(l)), rhs(std::move(r)) {}
                bool eval(const detail::VariantQueryContext &ctx) const override {
                        return lhs->eval(ctx) || rhs->eval(ctx);
                }
        private:
                std::unique_ptr<detail::VariantQueryNode> lhs, rhs;
};

class AndNode : public detail::VariantQueryNode {
        public:
                AndNode(std::unique_ptr<detail::VariantQueryNode> l,
                        std::unique_ptr<detail::VariantQueryNode> r) : lhs(std::move(l)), rhs(std::move(r)) {}
                bool eval(const detail::VariantQueryContext &ctx) const override {
                        return lhs->eval(ctx) && rhs->eval(ctx);
                }
        private:
                std::unique_ptr<detail::VariantQueryNode> lhs, rhs;
};

class NotNode : public detail::VariantQueryNode {
        public:
                explicit NotNode(std::unique_ptr<detail::VariantQueryNode> c) : child(std::move(c)) {}
                bool eval(const detail::VariantQueryContext &ctx) const override { return !child->eval(ctx); }
        private:
                std::unique_ptr<detail::VariantQueryNode> child;
};

class HasNode : public detail::VariantQueryNode {
        public:
                explicit HasNode(String k) : key(std::move(k)) {}
                bool eval(const detail::VariantQueryContext &ctx) const override {
                        return ctx.resolve(key).has_value();
                }
        private:
                String key;
};

// Always-true / always-false literal nodes (only emitted when an
// expression reduces to a bare boolean).
class BoolNode : public detail::VariantQueryNode {
        public:
                explicit BoolNode(bool v) : value(v) {}
                bool eval(const detail::VariantQueryContext &) const override { return value; }
        private:
                bool value;
};

class CmpNode : public detail::VariantQueryNode {
        public:
                enum Op { Eq, NotEq, Lt, LtEq, Gt, GtEq, Regex, Contains };

                CmpNode(Op o, Operand l, Operand r)
                        : op(o), lhs(std::move(l)), rhs(std::move(r)) {}

                bool eval(const detail::VariantQueryContext &ctx) const override {
                        // Regex / substring paths need string forms.
                        if(op == Regex) {
                                if(rhs.kind != Operand::IsRegex || !rhs.regex) return false;
                                auto lv = resolve(lhs, ctx);
                                if(!lv.has_value()) return false;
                                String s = lv->get<String>();
                                return std::regex_search(std::string(s.cstr(), s.byteCount()), *rhs.regex);
                        }
                        if(op == Contains) {
                                auto lv = resolve(lhs, ctx);
                                if(!lv.has_value()) return false;
                                auto rv = resolve(rhs, ctx);
                                if(!rv.has_value()) return false;
                                String ls = lv->get<String>();
                                String rs = rv->get<String>();
                                return ls.contains(rs);
                        }

                        auto lv = resolve(lhs, ctx);
                        auto rv = resolve(rhs, ctx);
                        // Missing key → any comparison is false (except !=).
                        if(!lv.has_value() || !rv.has_value()) {
                                return op == NotEq
                                       && lv.has_value() != rv.has_value();
                        }

                        // When one side is a key and the other is a bare String
                        // literal, try to coerce the literal to the key's
                        // spec-declared type so typed comparisons work.
                        Variant a = *lv;
                        Variant b = *rv;
                        if(lhs.kind == Operand::IsKey && rhs.kind == Operand::IsLiteral) {
                                b = coerceLiteralToKey(b, lhs.key, a, ctx);
                        } else if(rhs.kind == Operand::IsKey && lhs.kind == Operand::IsLiteral) {
                                a = coerceLiteralToKey(a, rhs.key, b, ctx);
                        }

                        switch(op) {
                        case Eq:    return a == b;
                        case NotEq: return a != b;
                        case Lt:    return orderVariants(a, b) == Ord::Less;
                        case LtEq:  {
                                Ord o = orderVariants(a, b);
                                return o == Ord::Less || o == Ord::Equal;
                        }
                        case Gt:    return orderVariants(a, b) == Ord::Greater;
                        case GtEq:  {
                                Ord o = orderVariants(a, b);
                                return o == Ord::Greater || o == Ord::Equal;
                        }
                        default: return false;
                        }
                }

        private:
                static std::optional<Variant> resolve(const Operand &o, const detail::VariantQueryContext &ctx) {
                        switch(o.kind) {
                        case Operand::IsKey:     return ctx.resolve(o.key);
                        case Operand::IsLiteral: return o.literal;
                        case Operand::IsRegex:   return Variant(); // not a value, used only as RHS of ~
                        }
                        return std::nullopt;
                }

                Op      op;
                Operand lhs, rhs;
};

// ============================================================
// Parser
// ============================================================

class Parser {
        public:
                explicit Parser(const String &src) : _src(src), _lex(src.cstr(), src.byteCount()) {
                        advance();
                }

                std::unique_ptr<detail::VariantQueryNode> parseExpr() {
                        auto node = parseOr();
                        if(!_err.isEmpty()) return nullptr;
                        if(_cur.kind != Tok::End) {
                                if(_err.isEmpty()) {
                                        _err = String::sprintf("unexpected token at col %d", _cur.column);
                                }
                                return nullptr;
                        }
                        return node;
                }

                const String &error() const { return _err; }

        private:
                const String &_src;
                Lexer  _lex;
                Token  _cur;
                String _err;

                void advance() {
                        _cur = _lex.next();
                        if(_err.isEmpty() && !_lex.error().isEmpty()) _err = _lex.error();
                }
                bool accept(Tok k) {
                        if(_cur.kind == k) { advance(); return true; }
                        return false;
                }
                bool expect(Tok k, const char *what) {
                        if(_cur.kind == k) { advance(); return true; }
                        if(_err.isEmpty()) {
                                _err = String::sprintf("expected %s at col %d", what, _cur.column);
                        }
                        return false;
                }
                void setErr(const String &msg) { if(_err.isEmpty()) _err = msg; }

                std::unique_ptr<detail::VariantQueryNode> parseOr() {
                        auto lhs = parseAnd();
                        while(lhs && _cur.kind == Tok::OrOr) {
                                advance();
                                auto rhs = parseAnd();
                                if(!rhs) return nullptr;
                                lhs = std::make_unique<OrNode>(std::move(lhs), std::move(rhs));
                        }
                        return lhs;
                }

                std::unique_ptr<detail::VariantQueryNode> parseAnd() {
                        auto lhs = parseNot();
                        while(lhs && _cur.kind == Tok::AndAnd) {
                                advance();
                                auto rhs = parseNot();
                                if(!rhs) return nullptr;
                                lhs = std::make_unique<AndNode>(std::move(lhs), std::move(rhs));
                        }
                        return lhs;
                }

                std::unique_ptr<detail::VariantQueryNode> parseNot() {
                        if(_cur.kind == Tok::Bang) {
                                advance();
                                auto child = parseNot();
                                if(!child) return nullptr;
                                return std::make_unique<NotNode>(std::move(child));
                        }
                        return parsePrimary();
                }

                std::unique_ptr<detail::VariantQueryNode> parsePrimary() {
                        if(_cur.kind == Tok::LParen) {
                                advance();
                                auto inner = parseOr();
                                if(!inner) return nullptr;
                                if(!expect(Tok::RParen, "')'")) return nullptr;
                                return inner;
                        }
                        if(_cur.kind == Tok::Has) {
                                advance();
                                if(!expect(Tok::LParen, "'(' after has")) return nullptr;
                                if(_cur.kind != Tok::Ident) {
                                        setErr(String::sprintf("expected key name inside has() at col %d",
                                                               _cur.column));
                                        return nullptr;
                                }
                                String key = _cur.text;
                                advance();
                                if(!expect(Tok::RParen, "')'")) return nullptr;
                                return std::make_unique<HasNode>(std::move(key));
                        }
                        if(_cur.kind == Tok::BoolLit) {
                                bool v = _cur.bval;
                                advance();
                                // A bare boolean can stand as a full expression.
                                if(!isRelop(_cur.kind)) return std::make_unique<BoolNode>(v);
                                // Otherwise fall through as an operand — we rewind by
                                // synthesising an Operand below.  Since the token is
                                // already consumed, build LHS directly here.
                                Operand lhs;
                                lhs.kind    = Operand::IsLiteral;
                                lhs.literal = Variant(v);
                                return parseComparisonTail(std::move(lhs));
                        }

                        // Otherwise: an operand followed by optional relop + operand.
                        Operand lhs;
                        if(!parseOperand(lhs)) return nullptr;
                        if(isRelop(_cur.kind)) {
                                return parseComparisonTail(std::move(lhs));
                        }
                        // Bare operand with no comparison:
                        // * key  => truthy when the resolver returns a value (delegate to HasNode)
                        // * literal (number / string / bool) => constant truth
                        if(lhs.kind == Operand::IsKey) {
                                return std::make_unique<HasNode>(std::move(lhs.key));
                        }
                        if(lhs.kind == Operand::IsLiteral) {
                                Error derr;
                                bool bv = lhs.literal.get<bool>(&derr);
                                if(derr.isOk()) return std::make_unique<BoolNode>(bv);
                                setErr(String::sprintf(
                                        "literal value at col %d cannot stand alone as a boolean; "
                                        "use a relational operator (==, !=, <, ...) or has()",
                                        _cur.column));
                                return nullptr;
                        }
                        setErr(String::sprintf("regex literal at col %d needs an LHS operand and '~'",
                                               _cur.column));
                        return nullptr;
                }

                std::unique_ptr<detail::VariantQueryNode> parseComparisonTail(Operand lhs) {
                        CmpNode::Op op;
                        switch(_cur.kind) {
                        case Tok::Eq:         op = CmpNode::Eq;       break;
                        case Tok::NotEq:      op = CmpNode::NotEq;    break;
                        case Tok::Lt:         op = CmpNode::Lt;       break;
                        case Tok::LtEq:       op = CmpNode::LtEq;     break;
                        case Tok::Gt:         op = CmpNode::Gt;       break;
                        case Tok::GtEq:       op = CmpNode::GtEq;     break;
                        case Tok::Tilde:      op = CmpNode::Regex;    break;
                        case Tok::TildeTilde: op = CmpNode::Contains; break;
                        default:
                                setErr(String::sprintf("expected relational operator at col %d",
                                                       _cur.column));
                                return nullptr;
                        }
                        advance();

                        Operand rhs;
                        if(op == CmpNode::Regex) {
                                if(_cur.kind != Tok::RegexLit) {
                                        setErr(String::sprintf("expected regex literal after '~' at col %d",
                                                               _cur.column));
                                        return nullptr;
                                }
                                rhs.kind = Operand::IsRegex;
                                try {
                                        rhs.regex = std::make_shared<std::regex>(
                                                std::string(_cur.text.cstr(), _cur.text.byteCount()));
                                } catch(const std::regex_error &e) {
                                        setErr(String::sprintf("invalid regex at col %d: %s",
                                                               _cur.column, e.what()));
                                        return nullptr;
                                }
                                advance();
                        } else {
                                if(!parseOperand(rhs)) return nullptr;
                        }
                        return std::make_unique<CmpNode>(op, std::move(lhs), std::move(rhs));
                }

                bool parseOperand(Operand &out) {
                        switch(_cur.kind) {
                        case Tok::IntLit: {
                                out.kind    = Operand::IsLiteral;
                                // Store as int64 so integer comparisons don't suffer
                                // from float rounding, yet still promote to double
                                // through Variant for cross-type numeric compare.
                                out.literal = Variant(static_cast<int64_t>(_cur.fval));
                                advance();
                                return true;
                        }
                        case Tok::FloatLit: {
                                out.kind    = Operand::IsLiteral;
                                out.literal = Variant(_cur.fval);
                                advance();
                                return true;
                        }
                        case Tok::StringLit: {
                                out.kind    = Operand::IsLiteral;
                                out.literal = Variant(_cur.text);
                                advance();
                                return true;
                        }
                        case Tok::BoolLit: {
                                out.kind    = Operand::IsLiteral;
                                out.literal = Variant(_cur.bval);
                                advance();
                                return true;
                        }
                        case Tok::RegexLit: {
                                // Only valid as the RHS of a ~ operator;
                                // parseComparisonTail handles that case directly.  A
                                // bare regex reaching here is an error.
                                setErr(String::sprintf("unexpected regex literal at col %d",
                                                       _cur.column));
                                return false;
                        }
                        case Tok::Ident: {
                                out.kind = Operand::IsKey;
                                out.key  = _cur.text;
                                advance();
                                return true;
                        }
                        default:
                                setErr(String::sprintf("expected a value or key at col %d",
                                                       _cur.column));
                                return false;
                        }
                }

                static bool isRelop(Tok k) {
                        return k == Tok::Eq || k == Tok::NotEq
                            || k == Tok::Lt || k == Tok::LtEq
                            || k == Tok::Gt || k == Tok::GtEq
                            || k == Tok::Tilde || k == Tok::TildeTilde;
                }
};

} // namespace

// ============================================================
// detail helpers referenced by the template
// ============================================================

namespace detail {

std::unique_ptr<VariantQueryNode> parseVariantQueryExpr(const String &expr,
                                                        String &errorDetail) {
        errorDetail.clear();
        Parser p(expr);
        auto root = p.parseExpr();
        if(!root) {
                errorDetail = p.error().isEmpty() ? String("parse failed") : p.error();
                return nullptr;
        }
        return root;
}

bool evalVariantQuery(const VariantQueryNode *root, const VariantQueryContext &ctx) {
        if(root == nullptr) return false;
        return root->eval(ctx);
}

} // namespace detail

// ============================================================
// VariantQuery template definitions
// ============================================================

template <typename T>
VariantQuery<T>::VariantQuery() = default;

template <typename T>
VariantQuery<T>::~VariantQuery() = default;

template <typename T>
VariantQuery<T>::VariantQuery(VariantQuery &&) noexcept = default;

template <typename T>
VariantQuery<T> &VariantQuery<T>::operator=(VariantQuery &&) noexcept = default;

template <typename T>
VariantQuery<T>::VariantQuery(String source, std::unique_ptr<detail::VariantQueryNode> root)
        : _source(std::move(source)), _root(std::move(root)) {
        // Invariant: the success-path constructor is only reached from
        // parse() after a non-null root was produced.  The runtime
        // assert guards against a future refactor wiring a null root
        // through this path (which would make isValid() lie about
        // success and make match() silently return false).
        assert(_root != nullptr);
}

template <typename T>
Result<VariantQuery<T>> VariantQuery<T>::parse(const String &expr) {
        String errDetail;
        auto root = detail::parseVariantQueryExpr(expr, errDetail);
        if(!root) {
                VariantQuery<T> q;
                q._source       = expr;
                q._errorDetail  = std::move(errDetail);
                return Result<VariantQuery<T>>(std::move(q), Error::ParseFailed);
        }
        return makeResult(VariantQuery<T>(expr, std::move(root)));
}

template <typename T>
bool VariantQuery<T>::isValid() const { return _root != nullptr; }

template <typename T>
bool VariantQuery<T>::match(const T &instance) const {
        if(!_root) return false;
        detail::VariantQueryContext ctx;
        ctx.resolve = [&instance](const String &key) -> std::optional<Variant> {
                return VariantLookup<T>::resolve(instance, key);
        };
        ctx.specFor = [](const String &key) -> const VariantSpec * {
                return VariantLookup<T>::specFor(key);
        };
        return detail::evalVariantQuery(_root.get(), ctx);
}

template <typename T>
const String &VariantQuery<T>::source() const { return _source; }

template <typename T>
const String &VariantQuery<T>::errorDetail() const { return _errorDetail; }

// ============================================================
// Explicit instantiations
// ============================================================

template class VariantQuery<Frame>;
template class VariantQuery<Image>;
template class VariantQuery<Audio>;

PROMEKI_NAMESPACE_END
