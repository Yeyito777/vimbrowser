#!/usr/bin/env python3
"""Remove source branches that can never target a supported vimbrowser host.

This intentionally starts with whole, single-platform conditions. Mixed
conditions are left untouched for a later expression-simplification pass. The
transform preserves unknown branches and recursively processes nested blocks.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


UNSUPPORTED = {
    "IS_ANDROID",
    "IS_CASTOS",
    "IS_CAST_ANDROID",
    "IS_CHROMEOS",
    "IS_CHROMEOS_ASH",
    "IS_CHROMEOS_DEVICE",
    "IS_CHROMEOS_LACROS",
    "IS_FUCHSIA",
    "IS_IOS",
    "IS_IOS_APP_EXTENSION",
    "IS_IOS_TVOS",
    "IS_WIN",
}

UNSUPPORTED_GN = {
    "is_android",
    "is_castos",
    "is_cast_android",
    "is_chromeos",
    "is_chromeos_ash",
    "is_chromeos_device",
    "is_chromeos_lacros",
    "is_fuchsia",
    "is_ios",
    "is_ios_app_extension",
    "is_ios_tvos",
    "is_win",
    "ozone_platform_wayland",
}

CPP_DIRECTIVE_RE = re.compile(
    r"^(?P<indent>\s*)#\s*(?P<kind>if|ifdef|ifndef|elif|else|endif)\b(?P<rest>.*)$"
)
GN_IF_RE = re.compile(r"^(?P<indent>\s*)if \((?P<cond>[^\n]+)\) \{\s*(?:#.*)?$")
GN_ELIF_RE = re.compile(
    r"^(?P<indent>\s*)\} else if \((?P<cond>.+)\) \{\s*(?:#.*)?$"
)
GN_ELIF_START_RE = re.compile(r"^(?P<indent>\s*)\} else if \(")
GN_ELSE_RE = re.compile(r"^(?P<indent>\s*)\} else \{\s*(?:#.*)?$")


def strip_cpp_comment(text: str) -> str:
    return text.split("//", 1)[0].strip()


def peel_parentheses(text: str) -> str:
    text = text.strip()
    while text.startswith("(") and text.endswith(")"):
        depth = 0
        wraps = True
        for index, char in enumerate(text):
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0 and index != len(text) - 1:
                    wraps = False
                    break
        if not wraps or depth != 0:
            break
        text = text[1:-1].strip()
    return text


def eval_cpp_condition(kind: str, expression: str) -> bool | None:
    expression = peel_parentheses(strip_cpp_comment(expression))
    negated = False
    if kind == "ifndef":
        negated = True
    elif kind not in {"if", "elif", "ifdef"}:
        return None

    if kind in {"if", "elif"} and expression.startswith("!"):
        negated = not negated
        expression = peel_parentheses(expression[1:])

    match = re.fullmatch(r"(?:PA_)?BUILDFLAG\((IS_[A-Z0-9_]+)\)", expression)
    if match:
        name = match.group(1)
    else:
        match = re.fullmatch(r"(?:defined\s*\()?\s*([A-Z][A-Z0-9_]+)\s*\)?", expression)
        if not match:
            return None
        name = match.group(1)

    if name in UNSUPPORTED or name in {"OS_WIN", "OS_IOS", "OS_FUCHSIA"}:
        value = False
    elif name in {"IS_POSIX"}:
        value = True
    else:
        return None
    return not value if negated else value


def cpp_directive(lines: list[str], index: int) -> tuple[str, str, list[str], int] | None:
    match = CPP_DIRECTIVE_RE.match(lines[index].rstrip("\n"))
    if not match:
        return None
    header = [lines[index]]
    end = index + 1
    while header[-1].rstrip().endswith("\\") and end < len(lines):
        header.append(lines[end])
        end += 1
    expression = match.group("rest")
    if len(header) > 1:
        expression = expression.rstrip().removesuffix("\\")
        expression += " " + " ".join(
            line.strip().removesuffix("\\") for line in header[1:]
        )
    return match.group("kind"), expression.strip(), header, end


def rewrite_cpp_header(header: list[str], old: str, new: str) -> list[str]:
    result = list(header)
    result[0] = re.sub(
        rf"^(\s*#\s*){re.escape(old)}\b", rf"\g<1>{new}", result[0], count=1
    )
    return result


def rewrite_cpp_as_else(header: list[str]) -> list[str]:
    indent = re.match(r"^\s*", header[0]).group(0)
    newline = "\n" if header[-1].endswith("\n") else ""
    return [f"{indent}#else{newline}"]


def transform_cpp_sequence(
    lines: list[str], index: int = 0, stops: frozenset[str] = frozenset()
) -> tuple[list[str], int]:
    output: list[str] = []
    while index < len(lines):
        directive = cpp_directive(lines, index)
        if directive and directive[0] in stops:
            break
        if not directive or directive[0] not in {"if", "ifdef", "ifndef"}:
            output.append(lines[index])
            index += 1
            continue

        first_kind, first_expr, first_header, body_start = directive
        branches: list[tuple[str, str, list[str], list[str], bool | None]] = []
        body, index = transform_cpp_sequence(
            lines, body_start, frozenset({"elif", "else", "endif"})
        )
        branches.append(
            (
                first_kind,
                first_expr,
                first_header,
                body,
                eval_cpp_condition(first_kind, first_expr),
            )
        )

        endif_header: list[str] | None = None
        while index < len(lines):
            boundary = cpp_directive(lines, index)
            if not boundary:
                raise ValueError(f"lost preprocessor boundary at line {index + 1}")
            kind, expression, header, next_index = boundary
            if kind == "endif":
                endif_header = header
                index = next_index
                break
            if kind not in {"elif", "else"}:
                raise ValueError(f"unexpected #{kind} at line {index + 1}")
            body, index = transform_cpp_sequence(
                lines, next_index, frozenset({"elif", "else", "endif"})
            )
            branches.append(
                (
                    kind,
                    expression,
                    header,
                    body,
                    True if kind == "else" else eval_cpp_condition(kind, expression),
                )
            )
        if endif_header is None:
            raise ValueError("unterminated preprocessor conditional")

        kept: list[tuple[str, str, list[str], list[str], bool | None]] = []
        for branch in branches:
            value = branch[4]
            if value is False:
                continue
            kept.append(branch)
            if value is True:
                break

        if not kept:
            continue
        if kept[0][4] is True:
            output.extend(kept[0][3])
            continue

        for branch_index, (kind, _expr, header, body, value) in enumerate(kept):
            if branch_index == 0:
                if kind == "elif":
                    header = rewrite_cpp_header(header, "elif", "if")
                output.extend(header)
            elif value is True:
                if kind != "else":
                    header = rewrite_cpp_as_else(header)
                output.extend(header)
            else:
                output.extend(header)
            output.extend(body)
        output.extend(endif_header)
    return output, index


def gn_brace_delta(line: str) -> int:
    line = line.split("#", 1)[0]
    escaped = False
    quoted = False
    delta = 0
    for char in line:
        if escaped:
            escaped = False
            continue
        if char == "\\" and quoted:
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted and char == "{":
            delta += 1
        elif not quoted and char == "}":
            delta -= 1
    return delta


def eval_gn_condition(expression: str) -> bool | None:
    expression = peel_parentheses(expression.strip())
    negated = expression.startswith("!")
    if negated:
        expression = peel_parentheses(expression[1:])
    if expression in UNSUPPORTED_GN:
        value = False
    else:
        return None
    return not value if negated else value


def parse_gn_if(
    lines: list[str], start: int
) -> tuple[list[tuple[str, str, str, list[str], bool | None]], str, int] | None:
    first = GN_IF_RE.match(lines[start].rstrip("\n"))
    if not first:
        return None
    branches: list[tuple[str, str, str, list[str], bool | None]] = []
    kind = "if"
    expression = first.group("cond")
    header = lines[start]
    body_start = start + 1
    depth = 1
    index = body_start
    while index < len(lines):
        stripped = lines[index].rstrip("\n")
        if depth == 1:
            elif_match = GN_ELIF_RE.match(stripped)
            elif_end = index + 1
            if not elif_match and GN_ELIF_START_RE.match(stripped):
                # GN commonly wraps a long `else if` condition across lines.
                # Parse the whole header before deciding where the prior branch
                # ends so continuation lines never become orphaned source.
                joined = stripped
                while elif_end < len(lines) and "{" not in joined:
                    joined += " " + lines[elif_end].strip()
                    elif_end += 1
                elif_match = GN_ELIF_RE.match(joined)
            else_match = GN_ELSE_RE.match(stripped)
            if elif_match or else_match:
                body = transform_gn(lines[body_start:index])
                branches.append(
                    (
                        kind,
                        expression,
                        header,
                        body,
                        True if kind == "else" else eval_gn_condition(expression),
                    )
                )
                if elif_match:
                    kind = "elif"
                    expression = elif_match.group("cond")
                    header_end = elif_end
                else:
                    kind = "else"
                    expression = ""
                    header_end = index + 1
                header = "".join(lines[index:header_end])
                body_start = header_end
                index = header_end
                continue
        depth += gn_brace_delta(lines[index])
        if depth == 0:
            body = transform_gn(lines[body_start:index])
            branches.append(
                (
                    kind,
                    expression,
                    header,
                    body,
                    True if kind == "else" else eval_gn_condition(expression),
                )
            )
            return branches, lines[index], index + 1
        index += 1
    raise ValueError(f"unterminated GN if block at line {start + 1}")


def rewrite_gn_header(header: str, kind: str, expression: str) -> str:
    indent = re.match(r"^\s*", header).group(0)
    newline = "\n" if header.endswith("\n") else ""
    if kind == "if":
        return f"{indent}if ({expression}) {{{newline}"
    if kind == "elif":
        return f"{indent}}} else if ({expression}) {{{newline}"
    return f"{indent}}} else {{{newline}"


def transform_gn(lines: list[str]) -> list[str]:
    output: list[str] = []
    index = 0
    while index < len(lines):
        parsed = parse_gn_if(lines, index)
        if not parsed:
            output.append(lines[index])
            index += 1
            continue
        branches, closing, index = parsed
        kept: list[tuple[str, str, str, list[str], bool | None]] = []
        for branch in branches:
            if branch[4] is False:
                continue
            kept.append(branch)
            if branch[4] is True:
                break
        if not kept:
            continue
        if kept[0][4] is True:
            output.extend(kept[0][3])
            continue
        for branch_index, (kind, expression, header, body, value) in enumerate(kept):
            if branch_index == 0:
                output.append(rewrite_gn_header(header, "if", expression))
            elif value is True:
                output.append(rewrite_gn_header(header, "else", ""))
            else:
                output.append(rewrite_gn_header(header, "elif", expression))
            output.extend(body)
        output.append(closing)
    return output


def self_test() -> None:
    cpp = """before
#if BUILDFLAG(IS_WIN)
win
#elif BUILDFLAG(IS_MAC)
mac
#else
linux
#endif
#if !BUILDFLAG(IS_IOS)
keep
#endif
#if BUILDFLAG(IS_MAC)
mac-again
#elif BUILDFLAG(IS_POSIX)
posix
#endif
after
""".splitlines(keepends=True)
    expected_cpp = """before
#if BUILDFLAG(IS_MAC)
mac
#else
linux
#endif
keep
#if BUILDFLAG(IS_MAC)
mac-again
#else
posix
#endif
after
"""
    actual, end = transform_cpp_sequence(cpp)
    assert end == len(cpp)
    assert "".join(actual) == expected_cpp

    gn = """source_set("x") {
  if (is_win) {
    sources += [ "win.cc" ]
  } else if (is_mac) {
    sources += [ "mac.cc" ]
  } else {
    sources += [ "linux.cc" ]
  }
  if (!is_ios) {
    defines = [ "KEEP" ]
  }
}
""".splitlines(keepends=True)
    expected_gn = """source_set("x") {
  if (is_mac) {
    sources += [ "mac.cc" ]
  } else {
    sources += [ "linux.cc" ]
  }
    defines = [ "KEEP" ]
}
"""
    assert "".join(transform_gn(gn)) == expected_gn


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--files-from", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("self-test: PASS")
        return 0

    paths = [Path(path) for path in args.paths]
    if args.files_from:
        paths.extend(
            Path(line.split("\t", 1)[0])
            for line in args.files_from.read_text().splitlines()
            if line and not line.startswith("path\t")
        )
    changed = 0
    removed_lines = 0
    for path in dict.fromkeys(paths):
        if not path.is_file():
            continue
        original = path.read_text(errors="surrogateescape")
        lines = original.splitlines(keepends=True)
        if path.suffix in {".gn", ".gni"} or path.name == "BUILDCONFIG.gn":
            transformed = transform_gn(lines)
        elif path.suffix in {".c", ".cc", ".h", ".m", ".mm"}:
            transformed, end = transform_cpp_sequence(lines)
            if end != len(lines):
                raise ValueError(f"unexpected top-level directive in {path}")
        else:
            continue
        result = "".join(transformed)
        if original.endswith("\n"):
            result = result.rstrip("\n") + "\n"
        if result == original:
            continue
        path.write_text(result, errors="surrogateescape")
        changed += 1
        removed_lines += len(lines) - len(transformed)
    print(f"changed_files={changed} net_removed_lines={removed_lines}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
