#!/usr/bin/env python3
"""Reflow Luna sources back toward their original house style under a
120-column budget.

The codebase was written with an aggressive one-parameter-per-line
convention. This tool merges obvious physical continuations so lines use
the width budget, WITHOUT respacing any tokens: indentation of the first
physical line is kept, continuation text is appended with the single
separator the syntax implies, and brace-block structure is untouched.

A physical line is merged into the previous one when
  * the previous line leaves an open parenthesis/bracket (never braces),
  * or it ends with a continuation operator (&& || ? : = -> comparison,
    arithmetic), including the assignment '=',
  * or the next line begins with a closing bracket / `.` / `&&` / `||`,
and the merged result stays within the budget.

Invariant per file: whitespace-insensitive token stream unchanged
(string literals masked length-for-length); import lines may be sorted.
"""

from __future__ import annotations

import pathlib
import re
import sys

BUDGET = 120

STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
TOKEN_RE = re.compile(
    r'"(?:[^"\\]|\\.)*"'
    r"|<<=|>>=|->|==|!=|<=|>=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\|=|\^="
    r"|<<|>>|[A-Za-z_][A-Za-z0-9_]*|[0-9][A-Za-z0-9_]*"
    r"|[-+*/%<>=&|^!:;,.\[\](){}?]"
)

TAIL_OPS = ("&&", "||", "->", "==", "!=", "<=", ">=", "<<", ">>",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
            "?", ":", "=", "+", "-", "*", "/", "%", "&", "|", "^", "<",
            ">", ",", "(")
HEAD_CONT = (")", "]", ".", "?", ":", "&&", "||")


def mask(text):
    return STRING_RE.sub(lambda m: '"\x00%d\x00"' % len(m.group(0)), text)


def token_stream(text):
    return "".join(TOKEN_RE.findall(mask(text)))


def bracket_depth(text):
    """Depth of () and [] only, braces ignored, strings masked."""
    masked = mask(text)
    depth = 0
    for character in masked:
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
    return depth


def tail_continues(line):
    stripped = line.rstrip()
    if not stripped:
        return False
    if stripped.endswith(";") or stripped.endswith("{"):
        return False
    if bracket_depth(stripped) > 0:
        return True
    for op in sorted(TAIL_OPS, key=len, reverse=True):
        if stripped.endswith(op):
            # '=' must be assignment/comparison, not '==' already covered;
            # avoid treating '=>' absent, fine.
            return True
    return False


def head_continues(line):
    stripped = line.lstrip()
    if not stripped:
        return False
    return stripped.startswith(HEAD_CONT)


def merge(left, right):
    left_stripped = left.rstrip()
    right_stripped = right.strip()
    if left_stripped.endswith("("):
        return left_stripped + right_stripped
    glue = ""
    if right_stripped[:1] not in (")", "]", ",", ".", ";", ":"):
        glue = " "
    return left_stripped + glue + right_stripped


def bracket_delta(text):
    masked = mask(text)
    delta = 0
    for character in masked:
        if character in "([":
            delta += 1
        elif character in ")]":
            delta -= 1
    return delta


def paren_group_end(lines, start):
    """Index just past the group opened on lines[start]; None if unbalanced."""
    depth = 1
    index = start + 1
    while index < len(lines) and depth > 0:
        depth += bracket_delta(lines[index])
        index += 1
    if depth != 0:
        return None
    return index


def format_text(text):
    lines = text.split("\n")
    changed = True
    while changed:
        changed = False
        out = []
        index = 0
        while index < len(lines):
            current = lines[index]
            if current.rstrip().endswith("("):
                end = paren_group_end(lines, index)
                if end is not None:
                    group = current
                    for member in lines[index + 1:end]:
                        group = merge(group, member)
                    if len(group) <= BUDGET:
                        out.append(group)
                        index = end
                        changed = True
                        continue
                out.append(current)
                index += 1
                continue
            while index + 1 < len(lines):
                nxt = lines[index + 1]
                if not nxt.strip():
                    break
                if nxt.strip().startswith("}"):
                    break
                if nxt.rstrip().endswith("("):
                    break
                candidate_len = len(merge(current, nxt))
                if candidate_len > BUDGET:
                    break
                if tail_continues(current) or head_continues(nxt):
                    current = merge(current, nxt)
                    index += 1
                    changed = True
                    continue
                break
            out.append(current)
            index += 1
        lines = out
    return "\n".join(lines) + ("\n" if text.endswith("\n") else "")


def sort_imports(lines):
    result = []
    run = []
    for line in lines:
        if line.strip().startswith("import "):
            run.append(line)
            continue
        if len(run) > 1:
            result.extend(sorted(run))
        else:
            result.extend(run)
        run = []
        result.append(line)
    if len(run) > 1:
        result.extend(sorted(run))
    else:
        result.extend(run)
    return result


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    targets = [
        *sorted((root / "compiler").rglob("*.la")),
        *sorted((root / "compiler").rglob("*.lh")),
        *sorted((root / "library").rglob("*.la")),
        *sorted((root / "library").rglob("*.lh")),
        *sorted((root / "drivers").rglob("*.la")),
    ]
    reformatted = 0
    skipped = 0
    for path in targets:
        original = path.read_text(encoding="utf-8")
        formatted = "\n".join(sort_imports(
            format_text(original).split("\n")))
        if formatted == original:
            continue
        original_lines = original.split("\n")
        formatted_lines = formatted.split("\n")
        def stream(lines):
            return "".join(token_stream(line) for line in lines
                           if not line.strip().startswith("import "))
        if stream(original_lines) != stream(formatted_lines):
            print("SKIP token-mismatch: %s"
                  % path.relative_to(root), file=sys.stderr)
            skipped += 1
            continue
        path.write_text(formatted, encoding="utf-8")
        reformatted += 1
    print("reformatted %d, skipped %d" % (reformatted, skipped))
    return 1 if skipped else 0


if __name__ == "__main__":
    raise SystemExit(main())
