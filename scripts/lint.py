#!/usr/bin/env python3
"""Repository checks that run on every pull request (.github/workflows/lint.yml). Standard library only, so any runner with Python 3 can run it.

What it checks, and why (each rule exists because of something that went wrong or that the guidelines ask for):

  json       every .json file of the repository parses (a leading byte order mark, which the editor writes, is allowed)
  secrets    no keys, tokens or passwords in tracked files; signing keys and .env files are not tracked (guidelines B5, E5)
  config     no URLs or IP addresses in the C++ source: where the product reads from comes from files and settings (guideline B5)
  headers    every C++ header starts with #pragma once
  unity      no file-scope "using namespace" in a test .cpp: the build joins .cpp files into one, and it leaks into the others (it broke the build once)
  links      every relative link of the Markdown documents points at a file that exists (guideline G2: one source of truth)

Usage: python3 scripts/lint.py [--root DIR]    Exit status 1 when anything is found.
"""
import argparse
import json
import os
import re
import subprocess
import sys

SKIP_DIRS = {".git", "node_modules", "Intermediate", "Binaries", "Saved", "DerivedDataCache", "Build", "site", "Content"}
TEXT_SUFFIXES = {".cpp", ".h", ".cs", ".md", ".json", ".yml", ".yaml", ".mjs", ".ini", ".uplugin", ".uproject", ".txt"}

SECRET_PATTERNS = [
    (re.compile(r"-----BEGIN (RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----"), "a private key"),
    (re.compile(r"\bgh[pousr]_[A-Za-z0-9]{30,}"), "a GitHub token"),
    (re.compile(r"\bAKIA[0-9A-Z]{16}\b"), "an AWS access key"),
    (re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}"), "a Slack token"),
    (re.compile(r"(?i)\b(password|passwd|api[_-]?key|secret)\b\s*[:=]\s*[\"'][^\"'\s]{6,}[\"']"), "a password or key written out"),
]
URL_IN_SOURCE = re.compile(r"https?://|\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b")
USING_NAMESPACE = re.compile(r"^\s*using namespace\b")
STRING_OR_CHAR = re.compile(r'"(?:\\.|[^"\\])*"|' + r"'(?:\\.|[^'\\])*'")
MARKDOWN_LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def tracked_files(root):
    """The files git tracks, or every file when this is not a git checkout."""
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True).stdout.decode("utf-8")
        return [f for f in out.split("\0") if f and os.path.isfile(os.path.join(root, f))]
    except (OSError, subprocess.CalledProcessError):
        found = []
        for base, dirs, names in os.walk(root):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            found += [os.path.relpath(os.path.join(base, n), root) for n in names]
        return found


def read(root, relative):
    with open(os.path.join(root, relative), "rb") as handle:
        return handle.read().decode("utf-8-sig", errors="replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    root = os.path.abspath(parser.parse_args().root)
    problems = []

    def problem(rule, relative, line, text):
        problems.append(f"{relative}:{line}: [{rule}] {text}" if line else f"{relative}: [{rule}] {text}")

    files = [f.replace("\\", "/") for f in tracked_files(root)]
    for relative in files:
        parts = relative.split("/")
        suffix = os.path.splitext(relative)[1].lower()
        name = parts[-1]
        if name == ".env" or suffix in (".jks", ".keystore", ".pem", ".p12"):
            problem("secrets", relative, 0, "a key or .env file must not be committed")
        if suffix not in TEXT_SUFFIXES or any(p in SKIP_DIRS for p in parts[:-1]):
            continue
        text = read(root, relative)
        lines = text.splitlines()

        if suffix in (".json", ".uplugin", ".uproject"):
            try:
                json.loads(text)
            except ValueError as error:
                problem("json", relative, 0, f"does not parse: {error}")

        in_source = relative.startswith("InNavi/Source/")
        depth = 0
        for number, line in enumerate(lines, 1):
            code_only = STRING_OR_CHAR.sub("", line).split("//")[0]
            for pattern, what in SECRET_PATTERNS:
                if pattern.search(line) and "example" not in relative.lower():
                    problem("secrets", relative, number, f"looks like {what}")
            if in_source and suffix in (".cpp", ".h"):
                code = line.split("//")[0]
                if URL_IN_SOURCE.search(code) and "Tests.cpp" not in relative:
                    problem("config", relative, number, "a URL or address in the source: read it from a file or a setting")
            if in_source and relative.endswith("Tests.cpp") and depth == 0 and USING_NAMESPACE.match(line):
                problem("unity", relative, number, "file-scope 'using namespace' leaks into the other files of a unity build")
            depth += code_only.count("{") - code_only.count("}")

        if in_source and suffix == ".h" and not any(l.strip() == "#pragma once" for l in lines[:12]):
            problem("headers", relative, 1, "missing #pragma once")

        if suffix == ".md":
            directory = os.path.dirname(relative)
            in_fence = False
            for number, line in enumerate(lines, 1):
                if line.lstrip().startswith("```"):
                    in_fence = not in_fence
                if in_fence:
                    continue
                for target in MARKDOWN_LINK.findall(re.sub(r"`[^`]*`", "", line)):
                    if re.match(r"^(https?:|mailto:|#|tel:)", target):
                        continue
                    path = target.split("#")[0]
                    if path and not os.path.exists(os.path.normpath(os.path.join(root, directory, path))):
                        problem("links", relative, number, f"link to a file that does not exist: {target}")

    for line in problems:
        print(line)
    print(f"{len(files)} files checked, {len(problems)} problem{'s' if len(problems) != 1 else ''}.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
