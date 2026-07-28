# Luna bootstrap frontend

The first self-hosting frontend stage is implemented in Luna and compiled
directly to x86-64 objects. It does not translate through C, call the hosted
C23 frontend, depend on libc or issue raw system calls. The C23 compiler
remains the trusted stage-0 compiler that builds these modules.

## Modules and artifacts

The frontend is split at the same boundary that the eventual self-hosted
compiler will use:

| Module | Responsibility | Direct dependencies |
| --- | --- | --- |
| `luna.bootstrap.frontend.lexer` | source validation, tokenization and lexical diagnostics | `luna.runtime`, `luna.std.bytes`, `luna.std.text` |
| `luna.bootstrap.frontend.parser` | literal validation, recursive-descent parsing, recovery and indexed syntax trees | lexer plus its direct runtime/standard-library dependencies |

`luna_sysroot` emits deterministic metadata and separately linked objects at:

```text
sysroot/luna/bootstrap/frontend/lexer.lmi
sysroot/luna/bootstrap/frontend/lexer.o
sysroot/luna/bootstrap/frontend/parser.lmi
sysroot/luna/bootstrap/frontend/parser.o
```

Neither object defines `_start`. Applications compile against the `.lmi`
graph and explicitly link the objects, the freestanding runtime and the
required standard-library objects.

## Source and token contract

`BootstrapSourceSpan` records a byte offset, byte length, one-based line and
one-based byte column. A source is a borrowed `StdTextView`; it must be valid
UTF-8 and contain no embedded NUL byte. The lexer recognizes the complete
currently implemented Luna keyword, punctuation and operator set. Identifiers
remain deliberately ASCII until the language defines Unicode identifier
normalization.

Tokens retain source spans rather than owning lexeme copies. The final token
is exactly one zero-length `end` token at the source length. The parser
validates token kind bounds, span bounds, ordering and the final-token
contract before consuming a caller-supplied token buffer.

Lexical mistakes are recoverable language diagnostics, not runtime failures.
An unexpected byte or unterminated quoted literal produces an `invalid` token
and a structured diagnostic. Invalid UTF-8, an embedded NUL, malformed public
buffers and allocation failures use `RuntimeError`.

## Indexed syntax tree

The parser produces a concrete, lossless-enough structural tree for the next
type-checking stage. Nodes cover module/import declarations, aggregate and
enum definitions, functions, type syntax, every statement form and the full
expression precedence hierarchy. Parentheses and all three `for` clauses
remain explicit nodes, which makes later diagnostics and source-preserving
tools deterministic.

Nodes never point into a growable allocation. Every relationship is a
`usize` index:

- `parent` identifies the unique owner;
- `first_child`, `last_child` and `next_sibling` form an ordered child list;
- `token_index` identifies the leading or operator token;
- `bootstrap_syntax_no_index()` is the only absent-index value.

The tree records its original token count so token indices can be checked
without retaining a pointer to the token buffer. `bootstrap_syntax_tree_is_valid`
checks storage shape, enum and flag bounds, span arithmetic, token indices,
parent/child agreement, sibling-list lengths, unique ownership, root
identity and reachability. The parser refuses more than 256 nested type or
expression contexts so malformed input cannot exhaust the target stack.

The representation is deliberately not the typed AST. It contains syntax and
source identity only. Name binding, canonical types, layouts, constant values
and control-flow legality belong to the next self-hosting module.

## Diagnostics and recovery

`BootstrapDiagnostic` is a fixed-layout record containing a diagnostic kind,
source span, expected token, found token and kind-specific detail. The lexer
and parser own separate diagnostic buffers so clients can distinguish lexical
and syntactic failures without parsing text messages.

The parser skips lexer-produced invalid tokens, inserts expected-token
diagnostics without fabricating pointers and guarantees forward progress at
block, aggregate, switch and top-level recovery boundaries. Malformed integer
and floating literals are validated independently of tokenization. A
successfully recovered parse has `RuntimeError.none` and a structurally valid
tree while one or both diagnostic buffers remain nonempty.

## Ownership

Luna does not yet have affine types, so ownership follows the explicit
standard-library convention:

1. `bootstrap_lex` transfers ownership of its token and diagnostic buffers.
2. `bootstrap_parse` borrows the source and token buffer and transfers
   ownership of its tree and parser diagnostics.
3. `bootstrap_frontend_parse` combines both obligations.
4. The matching `*_release` function consumes each successful or partial
   result exactly once.

A parse tree may outlive the source and token allocation only as an indexed
shape. Reading lexemes still requires the original borrowed source, and
resolving `token_index` still requires the token buffer.

## Correctness gates

The frontend is checked at independent boundaries:

1. GoogleTest compiles the real Luna interfaces and implementations through
   verified typed IR, assembly and object emission, checks strong-type
   rejection and enforces the freestanding source boundary.
2. A target executable verifies exact token contracts, valid full-grammar
   parsing, lexical recovery, syntax recovery and release behavior.
3. The same x86-64 executable parses every syntactically valid integration
   source, every Luna runtime/sysroot source including the frontend itself,
   64 fixed-seed generated programs and 66 fixed-seed malformed/recovery
   cases, including the nesting limit.
4. Integration checks deterministic metadata reproduction, missing-object
   link failure, object symbols and absence of a dynamic loader.
5. The bounded libFuzzer corpus contains a frontend-shaped full-syntax seed.

This completes the lexer/parser item in M4. The subsequent Luna type checker
and Typed IR construction are documented in
[the bootstrap middle-end contract](bootstrap-middleend.md). Luna x86-64
lowering and stage reproducibility remain separate milestones.
