#!/usr/bin/env python3
"""Abacus PreToolUse guards. Usage: guards.py <guard> [<guard> ...] < hook-payload.json

Each named guard inspects the tool call; the first refusal exits 2 with a
reason on stderr (the text the agent sees) and a line in the refusal log.

Guards:
  repo-fence        writes stay inside this repository; never into another repo
                    under any workspace root or registered in the fleet,
                    ~/.claude, ~/.abacus, or this repo's own control files;
                    the marked gate call in a hook is never removed or changed
  role-policy       (roles) writes only to the paths in .claude/role-policy.json
  role-commands     (roles) every shell command must match the allowlist in
                    .claude/role-policy.json "commands" (fail closed)
  foreign-bd        on another repository's board: reads, filing, and tagged
                    notes only; everything the owner controls is refused. The
                    planner (ABACUS_ROLE=planner) files and shapes drafts, and
                    labels or approves a bead only when an operator ruling names it
  destructive-git   force-push, history rewrites, hard resets, hook bypass...
  stage-writes      Dynamic Workflow stages may not push, commit, close beads, or change GitHub (gh)
                    or start claude; only the finishing script does that
  database-url      tests run only against databases whose name ends in _test
  operator-only     releases, tags, deploys and publishes are the operator's, and so is
                    `bd ship --force` (shipping before the bead is closed)
  secrets           no environment dumps, no printing secret variables, no reading
                    credential files, no credential-retrieval commands (bw, bws,
                    security, gh auth token, ...); `ps` is refused while secrets
                    are loaded. A bw inside the workspace (e.g. a repository's own
                    build), run alone as a literal path, may run any subcommand.
                    Pairs with redact.py.
  human-only        no forged operator rulings (quoted words must come from a
                    genuine operator turn or the logbook), no browser or GUI
                    control as the signed-in operator, no MCP writes to shared
                    systems (issues, comments, pages), no decision-record
                    writes/deletes without a one-time `abacus-authorize` grant
"""
import hashlib
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
import _shell as sh  # noqa: E402
import _secrets as sec  # noqa: E402

FILE_TOOLS = ("Edit", "Write", "MultiEdit", "NotebookEdit")
READ_TOOLS = ("Read", "Grep")
MESSAGE_TOOLS = ("SendMessage",)
PROTECTED_IN_REPO = (".claude", ".abacus", ".githooks", ".git")
CLAUDE_HOME_ALLOWED = ("plans", "jobs")
INTERPRETERS = {"python": "-c", "python3": "-c", "node": "-e", "perl": "-e", "ruby": "-e", "osascript": "-e",
                "deno": "eval", "php": "-r"}
WRITE_API = re.compile(r"open\(|write|unlink|remove|rmtree|rename|replace|mkdir|makedirs|copy|move|symlink|"
                       r"chmod|truncate|os\.system|subprocess|popen|spawn|exec|writeFile|appendFile|rmSync|"
                       r"File\.|IO\.|Path\(", re.I)
GIT_KNOWN = {
    "add", "am", "apply", "archive", "bisect", "blame", "branch", "bundle", "cat-file", "check-ignore",
    "checkout", "cherry", "cherry-pick", "clean", "clone", "commit", "config", "count-objects", "describe",
    "diff", "diff-tree", "fetch", "for-each-ref", "format-patch", "fsck", "gc", "grep", "help", "init", "log",
    "ls-files", "ls-remote", "ls-tree", "merge", "merge-base", "mv", "name-rev", "notes", "pull", "push",
    "range-diff", "rebase", "reflog", "remote", "replace", "reset", "restore", "rev-list", "rev-parse",
    "revert", "rm", "shortlog", "show", "show-ref", "sparse-checkout", "stash", "status", "submodule",
    "switch", "symbolic-ref", "tag", "update-index", "update-ref", "var", "verify-commit", "version",
    "whatchanged", "worktree", "filter-branch", "filter-repo", "lfs", "check-attr", "hash-object", "write-tree",
}
GIT_READ = {"status", "log", "show", "diff", "diff-tree", "rev-parse", "ls-files", "ls-tree", "ls-remote", "blame",
            "grep", "describe", "cat-file", "merge-base", "rev-list", "shortlog", "show-ref", "for-each-ref",
            "name-rev", "whatchanged", "count-objects", "var", "help", "version", "check-ignore", "check-attr",
            "range-diff", "cherry", "verify-commit", "archive", "format-patch", "fsck"}
GIT_LIST_ONLY = {"branch": {"-l", "--list", "-a", "-r", "-v", "-vv", "--show-current", "--contains",
                            "--merged", "--no-merged", "--all", "--remotes"},
                 "tag": {"-l", "--list", "--contains", "--points-at", "-n"},
                 "remote": {"-v", "show", "get-url"}, "stash": {"list", "show"},
                 "config": {"--get", "--get-all", "--list", "-l", "--get-regexp"},
                 "worktree": {"list"}, "notes": {"list", "show"}, "reflog": {"show"}}
GIT_PATH_SUBS = {"rm", "mv", "checkout", "restore", "apply", "clean", "add", "update-index", "sparse-checkout"}
# value-taking short flags per subcommand: stop expanding a cluster there (`-mfix` is -m "fix")
GIT_VALUED = {"commit": "mFCct", "push": "o", "checkout": "bB", "switch": "cC", "branch": "", "clean": "e",
              "reset": "", "tag": "mFu", "stash": "m"}
BD_VALUED = {"--actor", "--db", "--database", "-C", "--directory", "--dolt-auto-commit", "--mem-profile", "--profile"}
BD_READ = {"show", "view", "list", "ready", "blocked", "where", "stats", "statuses", "types", "prime",
           "recall", "memories", "search", "count", "info", "version", "help", "status", "children", "export"}
# The marked call adoption puts in each hook git runs. These must match GATE_BEGIN, GATE_END and GATE_HOOKS in
# lib/abacus_standards/common.py.
GATE_BEGIN, GATE_END = "# abacus:gate begin", "# abacus:gate end"
GATE_HOOKS = ("pre-commit", "pre-push")
_GATE = rf"(?:{re.escape(GATE_BEGIN)}|{re.escape(GATE_END)})"
GATE_MARK = re.compile(rb"(?<=\n)[ \t]*" + _GATE.encode())
GATE_LINE = re.compile(rf"^[ \t]*{_GATE}", re.M)
GATE_BLOCK = re.compile(rf"^[ \t]*{re.escape(GATE_BEGIN)}[^\n]*\n.*?^[ \t]*{re.escape(GATE_END)}[^\n]*(?:\n|\Z)",
                        re.M | re.S)
GATE_REMOVERS = {"rm", "rmdir", "mv", "shred", "unlink"}  # these take a hook with the directory that holds it


# ---------------------------------------------------------------- helpers

def git_parts(argv):
    """Return (dir override, subcommand, args, config pairs) for a git argv."""
    i, override, configs = 1, None, []
    while i < len(argv) and argv[i].startswith("-"):
        a = argv[i]
        if a == "-C" and i + 1 < len(argv):
            override = argv[i + 1]; i += 2; continue
        if a.startswith(("--git-dir", "--work-tree")):
            override = a.split("=", 1)[1] if "=" in a else (argv[i + 1] if i + 1 < len(argv) else None)
            i += 1 if "=" in a else 2; continue
        if a == "-c" and i + 1 < len(argv):
            configs.append(argv[i + 1])
            if argv[i + 1].lower().startswith("core.worktree="):
                override = argv[i + 1].split("=", 1)[1]
            i += 2; continue
        if a.startswith("--config-env"):
            configs.append(a.split("=", 1)[1] if "=" in a else (argv[i + 1] if i + 1 < len(argv) else ""))
        i += 1
    sub = argv[i] if i < len(argv) else ""
    return override, sub, argv[i + 1:], configs


def expand_flags(args, valued=""):
    """`-uf` -> {-u, -f}; stops at a value-taking letter so `-mfix` is just -m."""
    out = set()
    for a in args:
        if a.startswith("--") or not a.startswith("-") or len(a) <= 2:
            out.add(a.split("=", 1)[0] if a.startswith("--") else a)
            continue
        for ch in a[1:]:
            out.add("-" + ch)
            if ch in valued:
                break
    return out


def git_mutates(sub, args):
    if sub in GIT_READ or sub == "":
        return False
    if sub == "reflog":
        # `git reflog`, `git reflog -5`, `git reflog main` all mean `reflog show`; only expire/delete write
        return bool(args) and args[0] in ("expire", "delete")
    if sub in GIT_LIST_ONLY:
        return not (not args or any(a in GIT_LIST_ONLY[sub] for a in args))
    return True


def bd_words(argv):
    """Positional words of a bd call, skipping the values of global flags."""
    words, i = [], 1
    while i < len(argv):
        a = argv[i]
        if a in BD_VALUED:
            i += 2; continue
        if not a.startswith("-"):
            words.append(a)
        i += 1
    return words


def write_targets(seg):
    """Paths a shell segment writes to. Interpreter one-liners report the paths they name."""
    out = [r for r in seg.redirects if r not in ("/dev/null", "/dev/stderr", "/dev/stdout", "/dev/tty")]
    argv, cmd, cwd, env = seg.argv, seg.cmd, seg.cwd, seg.env
    if not argv:
        return out
    pos = [a for a in argv[1:] if not a.startswith("-")]
    exp = lambda a: sh._expand(a, cwd, env)
    if cmd in ("rm", "rmdir", "mkdir", "touch", "chmod", "chown", "chgrp", "truncate", "unlink", "shred", "mv", "tee",
               "ln", "mkfifo", "xattr", "setfacl"):
        out += [exp(a) for a in pos]
    elif cmd in ("cp", "install", "rsync", "scp", "ditto"):
        t = next((argv[i + 1] for i, a in enumerate(argv[:-1]) if a in ("-t", "--target-directory")), None)
        t = t or next((a.split("=", 1)[1] for a in argv if a.startswith("--target-directory=")), None)
        out += [exp(t)] if t else ([exp(pos[-1])] if pos else [])
    elif cmd in ("sed", "gsed") and any(a.startswith("-i") or a.startswith("--in-place") for a in argv):
        out += [exp(a) for a in sed_files(argv)]
    elif cmd in ("perl", "ruby") and any(a.startswith("-i") for a in argv):
        out += [exp(a) for a in pos]
    elif cmd == "dd":
        out += [exp(a[3:]) for a in argv if a.startswith("of=")]
    elif cmd in ("tar", "bsdtar", "unzip", "ditto"):
        d = next((argv[i + 1] for i, a in enumerate(argv[:-1]) if a in ("-C", "--directory", "-d")), None)
        extracting = cmd == "unzip" or any(a.startswith("-") and "x" in a.lstrip("-")[:4] or a == "x" or
                                           a.startswith("--extract") for a in argv[1:])
        creating = any(a.startswith("-") and "c" in a.lstrip("-")[:4] for a in argv[1:])
        if extracting:
            out.append(exp(d) if d else cwd)
        if creating:
            f = next((argv[i + 1] for i, a in enumerate(argv[:-1]) if a.startswith("-") and a.endswith("f")), None)
            out += [exp(f)] if f and f != "-" else []
    elif cmd == "find" and any(a in ("-delete", "-exec", "-execdir", "-ok", "-okdir", "-fprint") for a in argv):
        out += [exp(a) for a in argv[1:] if not a.startswith(("-", "(", ")", "!", "{")) and
                (a.startswith((".", "/", "~", "$")) or os.sep in a)][:8] or [cwd]
    elif cmd == "git":
        override, sub, args, _ = git_parts(argv)
        if git_mutates(sub, args):
            out.append(exp(override) if override else cwd)
            if sub in GIT_PATH_SUBS:
                base = exp(override) if override else cwd
                out += [sh._expand(a, base, env) for a in args if not a.startswith("-") and a != "--"]
    elif cmd in ("make", "gmake"):
        d = next((argv[i + 1] for i, a in enumerate(argv[:-1]) if a in ("-C", "--directory")), None)
        out.append(exp(d) if d else cwd)
    if cmd in INTERPRETERS and INTERPRETERS[cmd] in argv:
        i = argv.index(INTERPRETERS[cmd])
        code = argv[i + 1] if i + 1 < len(argv) else ""
        if WRITE_API.search(code):
            out += [exp(m) for m in re.findall(r"(?:~|/|\.\./)[\w./~-]+", code)]
            if ".." in code:
                out.append(os.path.dirname(cwd))  # relative escapes assembled at runtime: treat as the parent
    return out


def sed_files(argv):
    """Files an in-place sed edits. BSD takes `-i ''` (a separate, possibly empty, suffix); without -e/-f the first
    positional is the script, not a file."""
    files, script_given, i = [], False, 1
    while i < len(argv):
        a = argv[i]
        if a in ("-e", "-f", "--expression", "--file"):
            script_given = True; i += 2; continue
        if a.startswith(("--expression=", "--file=")):
            script_given = True; i += 1; continue
        if a == "-i" and i + 1 < len(argv) and (argv[i + 1] == "" or argv[i + 1].startswith(".")):
            i += 2; continue  # BSD suffix argument
        if a.startswith("-"):
            i += 1; continue
        files.append(a); i += 1
    return files if script_given else files[1:]


def unexpanded(targets):
    """Write targets that still hold a shell substitution we cannot resolve (arguments that are not
    write targets, such as quoted text being echoed, are never inspected)."""
    return [t for t in targets if "$" in t or "`" in t]


def bash_segments(data):
    try:
        return sh.parse(data["tool_input"].get("command", ""), data.get("cwd") or os.getcwd())
    except sh.Unparseable as e:
        sh.refuse("abacus-guards", f"{e}; rewrite the command more simply", data)


def file_targets(data):
    ti = data.get("tool_input") or {}
    p = ti.get("file_path") or ti.get("notebook_path")
    return [sh._expand(p, data.get("cwd") or os.getcwd())] if p else []


def _targets(data, segs):
    if data["tool_name"] in FILE_TOOLS:
        return file_targets(data), None
    targets = []
    for s in segs:
        t = write_targets(s)
        if t:
            bad = unexpanded(t)
            if bad:
                what = f"`{s.cmd} {git_parts(s.argv)[1]}`" if s.cmd == "git" else f"`{s.cmd}`"
                return [], (f"{what} writes (to {bad[0]}), and the path uses a shell variable or loop the fence cannot "
                            f"resolve. Spell the path out, or use a read-only form (e.g. `git ls-remote` instead of "
                            f"`git fetch`).")
        targets += t
    return targets, None


def gate_marked(path):
    """True when the file has a gate marker line. It reads the whole file, so padding a hook can't push one out of
    sight; a missing or unreadable file has none."""
    try:
        with open(path, "rb") as f:
            tail = b"\n"
            while True:
                chunk = f.read(1 << 20)
                if not chunk:
                    return False
                if GATE_MARK.search(tail + chunk):
                    return True
                tail = chunk[-128:]
    except OSError:
        return False


def gate_marked_at(path):
    """The marked file a write to path would touch: path itself, or a file directly in it when it is a directory."""
    if os.path.isfile(path):
        return path if gate_marked(path) else None
    if os.path.isdir(path):
        try:
            with os.scandir(path) as entries:
                return next((e.path for e in entries if e.is_file() and gate_marked(e.path)), None)
        except OSError:
            return None
    return None


def gate_hooks(d):
    """The hook files git runs for the repository holding directory d: <hooks dir>/<hook>, and the tracked
    .husky/<hook> that Husky's wrappers in .husky/_ call."""
    p = subprocess.run(["git", "-C", d, "rev-parse", "--path-format=absolute", "--git-path", "hooks"],
                       capture_output=True, text=True)
    dirs = [os.path.join(sh.toplevel(d), ".husky")]
    if p.returncode == 0 and p.stdout.strip():
        hooks = os.path.realpath(p.stdout.strip())
        dirs += [hooks, os.path.dirname(hooks)] if os.path.basename(hooks) == "_" else [hooks]
    return [os.path.join(x, h) for x in dirs for h in GATE_HOOKS]


def gate_edit_result(text, tool, ti):
    """The file's text after this file-tool call, or None when that can't be worked out exactly."""
    if tool == "Write":
        return ti.get("content") if isinstance(ti.get("content"), str) else None
    if tool == "MultiEdit":
        edits = ti.get("edits")
    elif tool == "Edit":
        edits = [ti]
    else:
        edits = None
    if not isinstance(edits, list) or not edits:
        return None
    for e in edits:
        old, new = (e.get("old_string"), e.get("new_string")) if isinstance(e, dict) else (None, None)
        if not isinstance(old, str) or not isinstance(new, str) or not old or old not in text:
            return None
        if e.get("replace_all"):
            text = text.replace(old, new)
        elif text.count(old) == 1:
            text = text.replace(old, new, 1)
        else:
            return None  # the tool itself refuses an ambiguous match
    return text


GATE_WHY = ("carries the Abacus gate call (`# abacus:gate begin` ... `# abacus:gate end`). Removing or changing it "
            "lets commits and pushes skip the gates, so agents never do; the operator makes such changes (re-running "
            "abacus-adopt restores the call).")


def gate_call_change(data, segs):
    """Refuse a write that could remove or alter the gate call in a hook. A file-tool edit is replayed and allowed
    when every block survives byte-identical; a shell write can't be replayed, so any one onto a marked file is
    refused."""
    tool = data["tool_name"]
    if tool in FILE_TOOLS:
        ti = data.get("tool_input") or {}
        for t in file_targets(data):
            if not gate_marked(t):
                continue  # includes a Write that creates the file
            with open(t, encoding="utf-8", errors="surrogateescape", newline="") as f:
                before = f.read()
            after = gate_edit_result(before, tool, ti)
            if after is None:
                return f"{t} {GATE_WHY} The guard can't tell what this edit leaves behind, so it refuses (fail closed)."
            if sorted(GATE_BLOCK.findall(before)) != sorted(GATE_BLOCK.findall(after)) or \
                    len(GATE_LINE.findall(before)) != len(GATE_LINE.findall(after)):
                return f"{t} {GATE_WHY} Edit other lines of the hook and leave the block exactly as it is."
        return None
    for s in segs:
        removed = write_targets(s) if s.cmd in GATE_REMOVERS else []
        if s.cmd == "git":
            override, sub, args, _ = git_parts(s.argv)
            if sub in ("rm", "mv"):
                base = sh._expand(override, s.cwd, s.env) if override else s.cwd
                removed = [sh._expand(a, base, s.env) for a in args if not a.startswith("-") and a != "--"]
        for t in write_targets(s):
            hit = gate_marked_at(t)
            if hit:
                return (f"{hit} {GATE_WHY} A shell write to it can't be checked; change other lines of the hook with "
                        f"the Edit tool.")
        for t in removed:
            if os.path.isdir(t):
                hit = next((h for h in gate_hooks(t) if sh.inside(h, t) and gate_marked(h)), None)
                if hit:
                    return f"{t} holds {hit}, which {GATE_WHY}"
    return None


# ---------------------------------------------------------------- guards

def repo_fence(data, segs):
    cwd = data.get("cwd") or os.getcwd()
    repo = sh.session_unit(cwd)          # the session's own worktree when isolated, else its repository
    main = sh.repo_identity(cwd)         # the main checkout every worktree of this repository shares
    in_worktree = repo != main and sh.inside(repo, sh.worktrees_dir(main))
    fenced = sh.workspace_roots() + sh.fleet_repos()
    claude_home = os.path.realpath(os.path.expanduser("~/.claude"))
    abacus = os.path.realpath(str(sh.abacus_home()))
    targets, problem = _targets(data, segs)
    if problem:
        return problem
    for t in targets:
        rt = os.path.realpath(t)
        if sh.inside(rt, repo):
            rel = os.path.relpath(sh._fold(rt), sh._fold(repo)).split(os.sep)[0]
            if rel in PROTECTED_IN_REPO:
                if not in_worktree and sh.inside(rt, sh.worktrees_dir(main)):
                    return (f"{t} is inside another session's worktree; each session writes only in its own "
                            f"checkout (enter your own with EnterWorktree)")
                return (f"{t} is part of the controls that govern this session ({rel}/). Agents never edit "
                        f"their own settings, hooks, workflows or Abacus config: describe the change and ask "
                        f"the operator to apply it (manual operator path).")
            continue
        if in_worktree and sh.inside(rt, main):
            return (f"{t} is outside your worktree ({repo}). You isolated into that worktree, so write only there; "
                    f"the main checkout and other worktrees belong to other sessions and the operator.")
        if sh.inside(rt, claude_home):
            sub = os.path.relpath(sh._fold(rt), sh._fold(claude_home)).split(os.sep)
            if sub[0] in CLAUDE_HOME_ALLOWED or (sub[0] == "projects" and "memory" in sub):
                continue
            return f"{t} is in the operator's ~/.claude; only memory, jobs and plans are writable"
        if sh.inside(rt, abacus):
            return f"{t} is Abacus control-plane state; write it only through the abacus-* CLIs"
        if any(sh.inside(rt, f) for f in fenced):
            return (f"{t} is outside this repository ({repo}). Never write into another repository: file a "
                    f"bead on its board or add a note tagged [from {os.path.basename(repo)}] instead.")
    return gate_call_change(data, segs)


def role_policy(data, segs):
    root = os.environ.get("CLAUDE_PROJECT_DIR") or data.get("cwd") or os.getcwd()
    try:
        policy = json.load(open(os.path.join(root, ".claude", "role-policy.json")))
    except (OSError, ValueError) as e:
        return f"role-policy.json unreadable ({e}); refusing every write (fail closed)"
    allowed = [os.path.realpath(os.path.join(root, p)) for p in policy.get("writable", [])]
    targets, problem = _targets(data, segs)
    if problem:
        return problem
    for t in targets:
        if not any(sh.inside(t, a) for a in allowed):
            return (f"{policy.get('name', 'this role')} may write only to {policy.get('writable')}; {t} is "
                    f"outside. Roles change the world through messages, beads and the logbook, never files.")
    return None


def _token_matches(token, p):
    """A `*` inside a token (`agent:orchestrator-*`) stands for a non-empty name: lowercase letters, digits, dashes."""
    if p == "*" or p == token:
        return True
    if "*" not in p:
        return False
    return re.fullmatch("[a-z0-9-]+".join(re.escape(part) for part in p.split("*")), token) is not None


def _matches(argv, pattern):
    """`*` as the last token matches any remainder; elsewhere it matches exactly one token. A `*` inside a token
    matches a name (see _token_matches)."""
    pat = pattern.split()
    if not pat or not argv or os.path.basename(argv[0]) != pat[0]:
        return False
    for i, p in enumerate(pat[1:], start=1):
        if p == "*" and i == len(pat) - 1:
            return True
        if i >= len(argv) or not _token_matches(argv[i], p):
            return False
    return len(argv) == len(pat)


def role_commands(data, segs):
    if data["tool_name"] != "Bash":
        return None
    root = os.environ.get("CLAUDE_PROJECT_DIR") or data.get("cwd") or os.getcwd()
    try:
        policy = json.load(open(os.path.join(root, ".claude", "role-policy.json")))
    except (OSError, ValueError) as e:
        return f"role-policy.json unreadable ({e}); refusing every command (fail closed)"
    allowed = policy.get("commands") or []
    for s in segs:
        if not s.argv:
            continue  # pure assignments or redirections are judged by the other guards
        if s.cmd in ("cd", "pwd", "true", "echo", "printf", "date", "sleep"):
            continue
        if not any(_matches(s.argv, p) for p in allowed):
            return (f"{policy.get('name', 'this role')} may not run `{' '.join(s.argv)[:120]}`. Allowed commands are "
                    f"listed in .claude/role-policy.json; anything else is the orchestrator's or the operator's job.")
    return None


def foreign_bd(data, segs):
    cwd = data.get("cwd") or os.getcwd()
    own = sh.repo_identity(cwd)          # a worktree's board is its main checkout's board
    own_name = os.path.basename(own)
    for s in segs:
        if s.cmd != "bd":
            continue
        argv = s.argv
        if "-C" in argv or any(a.startswith("--directory") for a in argv):
            return "`bd -C` is refused everywhere: cd into the repository so the fence can see where you are"
        db = next((argv[i + 1] for i, a in enumerate(argv) if a == "--db" and i + 1 < len(argv)), None)
        db = db or next((a.split("=", 1)[1] for a in argv if a.startswith("--db=")), None)
        for target in filter(None, (db, s.env.get("BEADS_DIR"), s.env.get("BEADS_DB"))):
            if sh.has_substitution(target) or not sh.inside(sh._expand(target, s.cwd, s.env), own):
                return f"pointing bd at another board ({target}) is refused; cd into that repository instead"
        board = own if s.cwd == cwd else sh.repo_identity(s.cwd)
        if sh.inside(board, own) and sh.role() == "worker":
            continue  # own board: the owner controls it
        words = bd_words(argv)
        verb = words[0] if words else ""
        if sh.role() == "planner":
            reason = planner_bd(argv, words, board)
            if reason:
                return reason
            continue
        tag = f"[from {own_name}]" if sh.role() == "worker" else "[from orchestrator]"
        if verb in BD_READ or (verb == "comments" and (len(words) < 2 or words[1] != "add")) \
                or (verb in ("dep", "label") and len(words) > 1 and words[1] in ("list", "list-all", "tree", "cycles")):
            continue
        if verb in ("create", "new", "q"):
            labels = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a in ("-l", "--labels", "--label")]
            labels += [a.split("=", 1)[1] for a in argv if a.startswith(("--labels=", "--label="))]
            if any(a.startswith(("--status", "-s")) for a in argv) or any("pipeline:" in l for l in labels):
                return "when filing on another board, do not set status or pipeline labels: the owner sizes it"
            continue
        if verb == "dep" and len(words) > 1 and words[1] == "add" and sh.role() == "orchestrator":
            continue  # the orchestrator records the dependency on the item it just filed
        if verb == "ship" and sh.role() == "orchestrator" and not any(a in ("--force", "-f") for a in argv):
            continue  # marks a capability delivered once the orchestrator has verified the producing bead
        text = " ".join(argv)
        if verb in ("comment", "note") or (verb == "comments" and len(words) > 1 and words[1] == "add"):
            if tag in text:
                continue
            return f"notes on another repository's board must begin with {tag}"
        if verb == "update":
            flags = {a.split("=", 1)[0] for a in argv if a.startswith("-")}
            if flags <= {"--append-notes", "--json", "-q", "--quiet"} and "--append-notes" in flags and tag in text:
                continue
            return (f"on another repository's board you may only append notes beginning with {tag}; title, "
                    f"acceptance, labels, status and claims belong to the owner")
        return f"`bd {verb}` on another repository's board is refused (the owner controls it)"
    return None


PLANNER_TAG = "[from planner]"
BD_TIMEOUT = 10      # seconds; a bd that hangs raises, and run() turns any raise into a refusal
MAX_BEADS = 50       # the planner names the beads, so it must not be able to name enough to outrun the hook
BD_VALUE_FLAGS = {"--title", "--description", "-d", "--body-file", "--acceptance", "--design", "--design-file",
                  "--append-notes", "--notes", "--external-ref", "--priority", "-p", "--status", "-s", "--assignee",
                  "-a", "--labels", "-l", "--add-label", "--remove-label", "--reason", "--type", "-t", "--db",
                  "--actor"}
PLANNER_DRAFT_EDITS = {"--title", "--description", "-d", "--body-file", "--stdin", "--acceptance", "--design",
                       "--design-file", "--append-notes", "--notes", "--external-ref", "--priority", "-p", "--json",
                       "-q", "--quiet"}


def bd_positionals(argv):
    """bd's positional words, skipping every flag and the value of a flag that takes one."""
    out, skip = [], False
    for a in argv[1:]:
        if skip:
            skip = False
        elif a.startswith("-"):
            skip = "=" not in a and a in BD_VALUE_FLAGS
        else:
            out.append(a)
    return out


def draft_beads(board, beads):
    """The beads among these that carry the draft label, from one `bd show`. bd leaves out ids it doesn't know."""
    p = subprocess.run(["bd", "show", *beads, "--json"], cwd=board, capture_output=True, text=True,
                       timeout=BD_TIMEOUT)
    try:
        data = json.loads(p.stdout or "null")
    except ValueError:
        return set()
    data = data if isinstance(data, list) else [data]
    return {d.get("id") for d in data if isinstance(d, dict) and "draft" in (d.get("labels") or [])}


def operator_ruled_on(bead):
    """True when a ruling recorded with this bead as an explicit target exists. Only the concierge records rulings,
    from words the operator typed (human-only guard); recording a ruling 'on' a bead afterwards doesn't count."""
    db = os.environ.get("ABACUS_LOGBOOK_DB") or str(sh.abacus_home() / "state" / "logbook.db")
    try:
        conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
        try:
            row = conn.execute("SELECT 1 FROM rulings WHERE target_item = ? UNION SELECT 1 FROM ruling_targets "
                               "WHERE bead = ? LIMIT 1", (bead, bead)).fetchone()
        finally:
            conn.close()
    except sqlite3.Error:
        return False
    return row is not None


def planner_bd(argv, words, board):
    """The planner files and shapes DRAFT beads on any fleet board. It never touches beads already approved, and it
    applies a pipeline label (or lifts `draft`) only where the operator's own ruling names the bead."""
    verb = words[0] if words else ""
    if verb in BD_READ or (verb in ("dep", "label") and len(words) > 1 and words[1] in ("list", "list-all", "tree")):
        return None
    if verb in ("create", "new", "q"):
        labels = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a in ("-l", "--labels", "--label")]
        labels += [a.split("=", 1)[1] for a in argv if a.startswith(("--labels=", "--label="))]
        labels = [x for l in labels for x in l.split(",")]
        if any(l.startswith("pipeline:") for l in labels) or any(a.startswith(("--status", "-s")) for a in argv):
            return "the planner files drafts: no status and no pipeline label (those come after the operator approves)"
        if "draft" not in labels:
            return "the planner files drafts only: add `-l draft` so nothing can run before the operator approves"
        return None

    def is_draft(bead):
        return bead in draft_beads(board, [bead])

    if verb == "update" and len(words) > 1:
        flags, skip = set(), False
        for a in argv:   # a flag's value can itself start with "-" (acceptance bullets do)
            if skip:
                skip = False
                continue
            if a.startswith("-"):
                flags.add(a.split("=", 1)[0])
                skip = "=" not in a and a.split("=", 1)[0] in BD_VALUE_FLAGS
        if flags <= {"--append-notes", "--json", "-q", "--quiet"} and PLANNER_TAG in " ".join(argv):
            return None
        if not is_draft(words[1]):
            return f"{words[1]} is not a draft: once approved, a bead belongs to its board's owner"
        if not flags <= PLANNER_DRAFT_EDITS:
            return (f"on a draft the planner may change title, description, acceptance, design, notes, source and "
                    f"priority; not {sorted(flags - PLANNER_DRAFT_EDITS)}")
        return None
    if verb == "dep" and len(words) > 2 and words[1] == "add":
        return None if is_draft(words[2]) else f"{words[2]} is not a draft; only drafts get new dependencies here"
    if verb == "close" and len(words) > 1:
        beads = bd_positionals(argv)[1:]
        if len(beads) > MAX_BEADS:
            return f"name at most {MAX_BEADS} beads per call"
        drafts = draft_beads(board, beads) if beads else set()
        bad = [b for b in beads if b not in drafts]
        return f"only drafts can be dropped by the planner ({', '.join(bad)} are not)" if bad else None
    if verb == "label" and len(words) > 3 and words[1] in ("add", "remove"):
        beads, label = words[2:-1], words[-1]
        if len(beads) > MAX_BEADS:
            return f"name at most {MAX_BEADS} beads per call"
        drafts = draft_beads(board, beads)
        for b in beads:
            if b not in drafts:
                return f"{b} is not a draft; its labels belong to its board's owner"
            if (label.startswith("pipeline:") or (label == "draft" and words[1] == "remove")) \
                    and not operator_ruled_on(b):
                return (f"{b}: no operator ruling names it. The pipeline label and approval come from the operator's "
                        f"own words, recorded by the concierge with this bead as a target")
        if label == "draft" and words[1] == "add":
            return None
        return None
    return f"`bd {verb}` is not something the planner does (it files and shapes drafts, never more)"


def destructive_git(data, segs):
    for s in segs:
        if s.cmd != "git":
            continue
        _, sub, args, configs = git_parts(s.argv)
        for c in configs:
            key = c.split("=", 1)[0].lower()
            if key == "core.hookspath":
                return "changing core.hooksPath bypasses the delivery gates"
            if key.startswith("alias."):
                return "defining git aliases inline hides what runs; use the real subcommand"
        if sub and sub not in GIT_KNOWN:
            return f"`git {sub}` is not a known git subcommand (aliases are refused: use the real command)"
        a = expand_flags(args, GIT_VALUED.get(sub, ""))
        low = [x.lower() for x in args]
        if sub == "config" and ("core.hookspath" in low or any(x.startswith("alias.") for x in low)):
            if not (set(args) & GIT_LIST_ONLY["config"]):
                return "changing core.hooksPath or aliases bypasses the gates"
        if "--no-verify" in a or (sub == "commit" and "-n" in a):
            return "--no-verify bypasses the gates; fix the failure instead"
        if sub == "push":
            if a & {"-f", "--force", "--force-with-lease", "--force-if-includes", "-d", "--delete", "--mirror",
                    "--prune"} or any(x.startswith(("--force", "--delete")) for x in args) or \
                    any(x.startswith(("+", ":")) or ":+" in x for x in args if not x.startswith("-")):
                return "force, delete and +refspec pushes are refused"
            if "--tags" in a or "--follow-tags" in a:
                return "pushing tags is a release act reserved for the operator"
        if sub == "reset" and a & {"--hard", "--merge", "--keep"}:
            return f"`git reset {' '.join(args)}` discards work"
        if sub == "checkout" and ("--" in args or "." in args or a & {"-f", "--force"}):
            return "`git checkout -f` / `git checkout -- <path>` discards uncommitted work"
        if sub == "switch" and a & {"-f", "--force", "--discard-changes"}:
            return "`git switch --discard-changes` discards uncommitted work"
        if sub == "restore" and not ("--staged" in a and "--worktree" not in a and "-W" not in a):
            return "`git restore` of the worktree discards uncommitted work (only `--staged` is allowed)"
        if sub == "clean" and "-f" in a | expand_flags(args, ""):
            return "`git clean -f` deletes untracked files"
        if sub == "stash" and (not args or args[0] not in ("list", "show", "create")):
            return "stash push/pop/drop hides or discards work"
        if sub == "worktree" and args and args[0] in ("add", "remove", "prune", "move"):
            return "worktree add/remove changes the repository layout"
        if sub in ("rebase", "filter-branch", "filter-repo", "replace", "update-ref", "symbolic-ref"):
            return f"`git {sub}` rewrites history or refs"
        if sub == "commit" and "--amend" in a:
            return "`git commit --amend` rewrites history"
        if sub == "branch" and a & {"-D", "--delete", "-d", "-M", "-m", "-f", "--force", "-C", "-c"}:
            return "deleting, renaming or force-moving branches is refused"
        if sub == "add" and (a & {"-A", "--all", "-u", "--update"} or "." in args or
                             not [x for x in args if not x.startswith("-")]):
            return "stage named paths only (`git add <path>...`), never -A, -u or ."
        if sub == "reflog" and args[:1] in (["expire"], ["delete"]):
            return "reflog expire/delete destroys recovery history"
        if sub == "gc" and any(x.startswith("--prune") for x in args):
            return "gc --prune destroys recovery history"
    return None


def stage_writes(data, segs):
    agent_type = (data.get("agent_type") or "").lower()
    in_stage = agent_type.startswith("workflow") or os.environ.get("ABACUS_STAGE") == "1"
    if not in_stage:
        return None
    for s in segs:
        if s.cmd == "git":
            _, sub, _, _ = git_parts(s.argv)
            if sub in ("push", "commit", "tag", "merge", "cherry-pick", "revert", "am"):
                return f"Dynamic Workflow stages may not `git {sub}`; only abacus-finalize commits and pushes"
        if s.cmd == "bd":
            words = bd_words(s.argv)
            joined = " ".join(s.argv)
            if words[:1] in (["close"], ["done"]) or re.search(r"(--status[= ]|-s )closed\b", joined):
                return "Dynamic Workflow stages may not close beads; only abacus-finalize closes"
        if s.cmd == "claude":
            return "Dynamic Workflow stages may not start claude sessions"
        if s.cmd == "gh":
            why = _gh_changes_github(s.argv)
            if why:
                return (f"Dynamic Workflow stages may not `{why}`; only abacus-finalize opens pull requests "
                        f"(a company skill supplies content through a profile slot, never the action)")
    return None


GH_CHANGES = {"pr": {"create", "merge", "close", "edit", "comment", "review", "ready", "reopen", "lock", "unlock"},
              "issue": {"create", "edit", "close", "comment", "reopen", "delete", "transfer", "lock", "unlock",
                        "pin", "unpin"},
              "release": {"create", "edit", "delete", "upload", "delete-asset"},
              "repo": {"create", "delete", "edit", "fork", "rename", "archive", "unarchive", "sync"},
              "label": {"create", "edit", "delete", "clone"}, "secret": {"set", "delete"},
              "variable": {"set", "delete"}, "workflow": {"run", "enable", "disable"}}


def _gh_changes_github(argv):
    """`gh <group> <verb>` that writes to GitHub, or `gh api` with a writing method or fields; else None."""
    words = [w for w in argv[1:] if not w.startswith("-")]
    if len(words) >= 2 and words[1] in GH_CHANGES.get(words[0], ()):
        return f"gh {words[0]} {words[1]}"
    if words[:1] == ["api"]:
        flags = argv[1:]
        method = next((flags[i + 1] for i, w in enumerate(flags[:-1]) if w in ("-X", "--method")), None)
        method = method or next((w.split("=", 1)[1] for w in flags if w.startswith("--method=")), None)
        if (method or "").upper() in ("POST", "PUT", "PATCH", "DELETE") or \
                (not method and any(w in ("-f", "-F", "--field", "--raw-field", "--input") or
                                    w.startswith(("--field=", "--raw-field=")) for w in flags)):
            return "gh api (a writing request)"
    return None


def database_url(data, segs):
    runners = {"make", "gmake", "npm", "yarn", "pnpm", "go", "dotnet", "cargo", "uv", "poetry", "npx", "tox", "nox"}
    for s in segs:
        for k, v in s.env.items():
            if k.endswith("TEST_DATABASE_URL") and not _db_name(v).endswith("_test"):
                return f"{k} must point at a database whose name ends in _test (got {_db_name(v)!r})"
        argv = s.argv
        is_test = s.cmd in ("pytest", "py.test", "jest", "vitest", "mocha", "rspec") or \
            "pytest" in argv[1:4] or (s.cmd in runners and any(w in ("test", "gate", "check") for w in argv[1:3])) or \
            (s.cmd.startswith("python") and argv[1:3] in (["-m", "pytest"], ["-m", "unittest"]))
        url = s.env.get("DATABASE_URL")
        if is_test and url and not _db_name(url).endswith("_test"):
            return f"tests may only run against a *_test database (DATABASE_URL names {_db_name(url)!r})"
    return None


def _db_name(url):
    return url.rstrip("/").rsplit("/", 1)[-1].split("?", 1)[0]


def operator_only(data, segs):
    for s in segs:
        if s.cmd in ("make", "gmake"):
            targets = [a for a in s.argv[1:] if not a.startswith("-") and "=" not in a]
            hit = [t for t in targets if t in ("deploy", "release", "publish")]
            if hit:
                return f"`make {hit[0]}` is an operator-only target; ask the operator in this session"
        if s.cmd == "git":
            _, sub, args, _ = git_parts(s.argv)
            if sub == "tag" and git_mutates(sub, args):
                return "tags and releases are reserved for the operator"
        if s.cmd == "gh" and s.argv[1:2] == ["release"] and s.argv[2:3] not in (["list"], ["view"]):
            return "`gh release` is reserved for the operator"
        if s.cmd in ("npm", "cargo", "dotnet", "twine", "yarn", "pnpm") and \
                any(a in ("publish", "upload", "push") for a in s.argv[1:3]):
            return "publishing packages is reserved for the operator"
        is_ship = s.cmd == "bd" and bd_words(s.argv)[:1] == ["ship"]  # global flags may come before ship
        force_requested = any(a in ("--force", "-f") or a.startswith("--force=") for a in s.argv[1:])
        if is_ship and force_requested:
            return ("`bd ship --force` marks a capability delivered before its bead is closed; that call is the "
                    "operator's. Ship it once the bead is closed and verified.")
    return None


# ---------------------------------------------------------------- secrets

NO_READ_CMDS = {"ls", "stat", "test", "[", "rm", "touch", "chmod", "chown", "mkdir", "rmdir", "unlink", "basename",
                "dirname", "realpath", "readlink", "file"}
SECRET_REF = re.compile(r"\$\{?([A-Za-z_][A-Za-z0-9_]*)")
PRINTERS = {"echo", "printf", "print", "cat", "tee", "logger", "say", "pbcopy", "wall"}
ENV_CODE = re.compile(r"os\.environ|getenv|process\.env|ENV\[|\$ENV|GetEnvironmentVariable|System\.getenv")
PRINT_CODE = re.compile(r"print|console\.|puts|echo|write|dump|log\(|\bp\b|pp\b|say")
BW_SAFE = {"status", "sync", "lock", "logout", "config", "--version", "-v", "--help", "-h", "help", "update",
           "completion"}
GIT_SECRET_OK = {"check-ignore", "ls-files", "status", "rm"}


def _repository_bw(s, sole):
    """A bw the repository built (target/debug/bw, a worktree's build) runs any subcommand: it's the repository's own
    product, built from code the session already runs through cargo or its tests. Anything else is the installed
    Bitwarden CLI, which reaches the operator's real vault, and stays guarded. Judged by which binary, never by
    subcommand, so a command the work is adding is covered too.

    An allow-list, because the guard reads command text and can't follow the shell: the exemption applies only when
    the whole Bash command is this one simple command (sole: nothing before it can cd or swap the file), given as a
    literal path (no $, backtick or ~ for the shell to rewrite), to a file that already exists inside the workspace
    and isn't a copy of the installed bw. node_modules (a dependency, not the product) never counts."""
    first = s.argv[0] if s.argv else ""
    if not sole or "/" not in first or any(c in first for c in "$`~"):
        return False
    path = os.path.realpath(os.path.join(s.cwd, first))
    if not os.path.isfile(path) or "node_modules" in path.split(os.sep) or _installed_bw_copy(path):
        return False
    roots = sh.workspace_roots() + sh.fleet_repos()
    project = os.environ.get("CLAUDE_PROJECT_DIR")
    if project:
        roots += [os.path.realpath(project), _git_main_checkout(project)]
    return any(sh.inside(path, r) for r in roots if r)


def _installed_bw_copy(path):
    """Whether path holds the same bytes as the bw on PATH (the installed CLI, copied into the repository)."""
    installed = shutil.which("bw")
    if not installed:
        return False
    installed = os.path.realpath(installed)
    try:
        if installed == path or os.path.getsize(installed) != os.path.getsize(path):
            return installed == path
        digest = lambda f: hashlib.sha256(open(f, "rb").read()).hexdigest()
        return digest(installed) == digest(path)
    except OSError:
        return True  # can't tell: treat it as the installed one


def _git_main_checkout(path):
    """The main checkout of the git repository at path, or None when git can't confirm one (never a guess)."""
    p = subprocess.run(["git", "-C", path, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                       capture_output=True, text=True)
    if p.returncode != 0 or not p.stdout.strip():
        return None
    common = os.path.realpath(p.stdout.strip())
    return os.path.dirname(common) if os.path.basename(common) == ".git" else common


def _secret_retrieval(s, words, sole=False):
    cmd, argv = s.cmd, s.argv
    if cmd == "bw" and not _repository_bw(s, sole):
        if words and words[0] not in BW_SAFE or not words and any(a not in BW_SAFE for a in argv[1:]):
            return ("Bitwarden CLI vault access (bw get/list/unlock/export/serve...) is a human-only act: agents never "
                    "read vault items. Ask the operator in this session if a value is genuinely needed.")
    if cmd == "bws":
        if words[:1] == ["secret"] and words[1:2] in (["get"], ["list"]):
            return "`bws secret get/list` prints Secrets Manager values; agents never read secret values"
        if words[:1] == ["run"] and "--" in argv:
            tail = argv[argv.index("--") + 1:]
            if tail and os.path.basename(tail[0]) in ("env", "printenv", "set", "export", "echo", "printf", "cat"):
                return "`bws run` into an environment dump or print would expose injected secrets"
    if cmd == "security":
        sub = words[0] if words else ""
        if sub in ("find-generic-password", "find-internet-password") and ("-w" in argv or "-g" in argv):
            return "printing keychain passwords is refused"
        if sub in ("dump-keychain", "export", "export-identities"):
            return f"`security {sub}` exports keychain secrets"
    if cmd == "gh" and (argv[1:3] == ["auth", "token"] or
                        (argv[1:3] == ["auth", "status"] and ("-t" in argv or "--show-token" in argv))):
        return "printing the GitHub token is refused"
    if cmd == "git":
        _, sub, args, _ = git_parts(argv)
        if sub == "credential" and args[:1] in (["fill"], ["approve"]) or sub.startswith("credential-"):
            return "git credential helpers print stored credentials"
    if cmd.startswith("git-credential-"):
        return "git credential helpers print stored credentials"
    if cmd == "aws":
        j = " ".join(words)
        if re.search(r"configure (get|export-credentials)|sts (get-session-token|assume-role|get-federation-token)|"
                     r"secretsmanager get-secret-value|ssm get-parameters?\b.*", j) and \
                (not j.startswith("ssm") or "--with-decryption" in argv):
            return "this aws command prints credentials or secret values"
    if cmd == "op" and (words[:1] == ["read"] or "--reveal" in argv or words[:1] == ["inject"]):
        return "1Password CLI secret reads are refused"
    if cmd == "vault" and (words[:1] == ["read"] or words[:2] == ["kv", "get"]):
        return "Vault secret reads are refused"
    if cmd == "kubectl" and words[:1] == ["get"] and words[1:2] and words[1].startswith("secret") and \
            any(a.startswith(("-o", "--output")) for a in argv):
        return "`kubectl get secret -o ...` prints secret data"
    if cmd == "az" and words[:3] == ["keyvault", "secret", "show"]:
        return "Key Vault secret reads are refused"
    if cmd == "gcloud" and words[:3] == ["secrets", "versions", "access"]:
        return "Secret Manager reads are refused"
    if cmd == "dotnet" and words[:2] == ["user-secrets", "list"]:
        return "`dotnet user-secrets list` prints secret values"
    return None


def secrets(data, segs):
    tool = data["tool_name"]
    cwd = data.get("cwd") or os.getcwd()
    root = sh.repo_root(cwd)
    extra = sec.extra_names(root)
    if tool != "Bash":
        if tool == "Write":
            return None  # writing a template or dummy .env leaks nothing; reading does
        ti = data.get("tool_input") or {}
        for p in filter(None, (ti.get("file_path"), ti.get("path"), ti.get("notebook_path"), ti.get("glob"))):
            if sec.is_secret_path(p, cwd):
                return (f"{p} is a credential file; agents never read or edit credential stores. Work from a "
                        f"template such as .env.example, or ask the operator.")
        return None
    loaded = sec.loaded_secrets(extra=extra)
    raw = str((data.get("tool_input") or {}).get("command") or "")
    sole = len(segs) == 1 and not any(c in raw for c in "$`~\n")  # one simple command the shell won't rewrite
    for s in segs:
        words, argv, cmd = s.words, s.argv, s.cmd
        if words and os.path.basename(words[0]) == "env" and not argv:
            return "dumping the environment is refused (it holds secrets); read a single non-secret variable instead"
        if cmd == "printenv" and (not argv[1:] or any(sec.is_secret_name(a, extra) for a in argv[1:])):
            return "printenv of the whole environment or of a secret variable is refused"
        if cmd == "set" and not argv[1:]:
            return "bare `set` dumps every variable, including secrets"
        if cmd == "export" and (not argv[1:] or argv[1:] == ["-p"]):
            return "bare `export` dumps exported variables, including secrets"
        if cmd in ("declare", "typeset") and argv[1:] and all(a.startswith("-") for a in argv[1:]) and \
                any(c in "".join(argv[1:]) for c in "px"):
            return f"`{cmd} {' '.join(argv[1:])}` dumps variables, including secrets"
        if cmd == "compgen" and "-e" in argv and loaded:
            return "listing exported variables while secrets are loaded is refused"
        if loaded and ((cmd == "set" and ("-x" in argv or ("-o" in argv and "xtrace" in argv))) or
                       (cmd in sh.SHELLS and any(a.startswith("-") and not a.startswith("--") and "x" in a
                                                 for a in argv[1:2]))):
            return "shell tracing (set -x / bash -x) echoes expanded secrets; refused while secrets are loaded"
        if cmd == "ps":
            if any(a == "-E" or (a.startswith("-") and not a.startswith("--") and "E" in a) or
                   (not a.startswith("-") and "e" in a) for a in argv[1:]):
                return "`ps` with environment display (e / -E) exposes secrets"
            if loaded:
                return (f"`ps` is refused while secrets are loaded in this session ({len(loaded)} secret "
                        f"variable(s)): process listings can expose them. Use `pgrep <name>` to test liveness.")
        if cmd == "pgrep" and loaded and any(a in ("-l", "-a", "--list-full", "-lf", "-fl", "-af", "-fa")
                                             for a in argv):
            return "`pgrep -l/-a` prints command lines; refused while secrets are loaded"
        refs = [m for a in argv[1:] for m in SECRET_REF.findall(a) if sec.is_secret_name(m, extra)]
        if refs and cmd in PRINTERS:
            return (f"printing or writing ${refs[0]} would expose a secret; pass it to the command that needs it "
                    f"instead (its output is redacted)")
        if cmd in INTERPRETERS and INTERPRETERS[cmd] in argv:
            i = argv.index(INTERPRETERS[cmd])
            code = argv[i + 1] if i + 1 < len(argv) else ""
            if ENV_CODE.search(code) and PRINT_CODE.search(code):
                return "interpreter one-liners that print the environment are refused"
        git_ok = cmd == "git" and git_parts(argv)[1] in GIT_SECRET_OK
        if cmd not in NO_READ_CMDS and not git_ok:
            candidates = [a.split("=", 1)[1] if a.startswith("-") and "=" in a else a for a in argv[1:]]
            candidates = [c for c in candidates if c and not c.startswith("-")] + list(s.inputs)
            for c in candidates:
                if sec.is_secret_path(sh._expand(c, s.cwd, s.env), s.cwd):
                    return (f"`{cmd}` on {c} would read a credential file; agents never read credential stores. "
                            f"Use a template such as .env.example, or ask the operator.")
        why = _secret_retrieval(s, [w for w in argv[1:] if not w.startswith("-")], sole)
        if why:
            return why
    return None


# ---------------------------------------------------------------- human-only

BROWSER_MCP = re.compile(r"^mcp__.*(chrome|browser|playwright|puppeteer|computer|selenium|webdriver)", re.I)
# An MCP tool that changes a shared system (a Jira issue, a comment, a link, a page) acts as the signed-in operator.
# Reading through MCP is fine; the first verb in the tool's own name decides, wherever it sits (`create_issue`,
# `slack_send_message`, `issue_create`, `API-post-page`), so `get_merge_request` stays a read. Names split into words
# at _, -, . and camelCase humps, so `settings` or `addressBook` don't read as `set` or `add`.
SHARED_WRITE_VERBS = {"create", "update", "delete", "add", "link", "unlink", "transition", "edit", "post", "assign",
                      "move", "remove", "set", "close", "reopen", "comment", "merge", "publish", "archive", "upload",
                      "send", "submit", "approve", "patch", "put", "push", "write"}
READ_VERBS = {"get", "list", "search", "read", "fetch", "find", "query", "describe", "view", "show", "lookup"}


def shared_write_mcp(name):
    for word in re.findall(r"[A-Za-z]+", re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)):
        word = word.lower()
        if word in READ_VERBS or word in SHARED_WRITE_VERBS:
            return word in SHARED_WRITE_VERBS
    return False
BROWSER_APPS = re.compile(r"chrome|chromium|safari|firefox|arc|edge|brave|opera|vivaldi|orion|bitwarden", re.I)
REAL_PROFILE = re.compile(r"Library/Application Support/(Google/Chrome|Chromium|BraveSoftware|Microsoft Edge|Arc)|"
                          r"Library/Application Support/Firefox|Library/Safari|\.config/(google-chrome|chromium)|"
                          r"\.mozilla/firefox", re.I)
DECISION_DIRS = ("docs/adr", "docs/decisions")
RULED = re.compile(r'ruled:\s*"([^"]+)"')
STANDING_WORDS = re.compile(r'STANDING[^\n]*?words:\s*"([^"]+)"')


def operator_quotes(text):
    """Operator words quoted in JUDGEMENT `ruled:` and STANDING `words:` fields (OUTCOME rows quote evidence)."""
    return RULED.findall(text) + STANDING_WORDS.findall(text)
OPERATOR_SOURCES = {"typed", "sdk", "cli", "human", "suggestion_accepted"}
HUMAN_ORIGINS = {"human", "sdk"}


def _is_operator_turn(d):
    """A turn the operator sent, however it was entered (typed, accepted suggestion, SDK prompt).

    Claude Code marks these with turnOrigin / origin.kind "human"; promptSource says *how* it was entered.
    """
    origin = d.get("origin") if isinstance(d.get("origin"), dict) else {}
    return (d.get("turnOrigin") in HUMAN_ORIGINS or origin.get("kind") in HUMAN_ORIGINS
            or d.get("promptSource") in OPERATOR_SOURCES)


def _norm(t):
    return " ".join(str(t).replace("\u201c", '"').replace("\u201d", '"').split()).casefold()


def operator_turns(transcript_path):
    """Text of genuine operator turns: typed or SDK-submitted prompts, never tool results or injected text."""
    out = []
    with open(transcript_path) as f:
        for line in f:
            try:
                d = json.loads(line)
            except ValueError:
                continue
            if d.get("type") != "user" or d.get("isMeta") or d.get("isSidechain"):
                continue
            if not _is_operator_turn(d):
                continue
            c = d.get("message", {}).get("content")
            if isinstance(c, list):
                if any(isinstance(b, dict) and b.get("type") == "tool_result" for b in c):
                    continue
                c = " ".join(b.get("text", "") for b in c if isinstance(b, dict))
            if isinstance(c, str) and "<task-notification>" not in c:
                out.append(_norm(re.sub(r"<(agent-message|system-reminder)[^>]*>.*?</\1>", " ", c, flags=re.S)))
    return out


def logbook_words():
    import sqlite3
    db = sh.abacus_home() / "state" / "logbook.db"
    if not db.exists():
        return []
    try:
        conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
        return [_norm(r[0]) for r in conn.execute("SELECT words FROM rulings")]
    except sqlite3.Error:
        return []


def _expand_vars(text, env):
    """Substitute $NAME / ${NAME} from variables the command itself assigned (export or NAME=...)."""
    return re.sub(r"\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?", lambda m: env.get(m.group(1), m.group(0)), text or "")


def _unattested(words, data):
    """Return the first quoted operator utterance that cannot be found, or None."""
    if not words:
        return None
    tp = data.get("transcript_path")
    try:
        turns = operator_turns(tp) if tp else []
    except OSError:
        turns = []
    known = logbook_words()
    for w in words:
        n = _norm(w)
        if not n:
            continue
        if not any(n in t for t in turns) and not any(n in k for k in known):
            return w
    return None


def _consume_grant(action, data):
    """Spend one unexpired operator grant for this action and repository, if any."""
    import time
    d = sh.abacus_home() / "state" / "authorizations"
    if not d.is_dir():
        return False
    here = sh.repo_root(data.get("cwd") or os.getcwd())
    for f in sorted(d.glob("*.json")):
        try:
            g = json.loads(f.read_text())
        except (OSError, ValueError):
            continue
        if g.get("action") != action or g.get("expires", 0) < time.time():
            continue
        if g.get("repo") not in ("*", None) and not sh.inside(here, g["repo"]):
            continue
        g["used_at"] = time.time()
        g["used_for"] = json.dumps(data.get("tool_input"))[:300]
        try:
            used = f.with_name(f.name + ".used")
            used.write_text(json.dumps(g, indent=2))
            f.unlink()
        except OSError:
            return False
        return True
    return False


def _decision_targets(data, segs):
    targets, _ = _targets(data, segs)
    if data["tool_name"] == "Bash":
        for s in segs:  # mv/git mv sources and rm targets are already covered; also catch git rm paths
            if s.cmd == "git":
                _, sub, args, _ = git_parts(s.argv)
                if sub in ("rm", "mv"):
                    targets += [sh._expand(a, s.cwd, s.env) for a in args if not a.startswith("-")]
    hits = []
    for t in targets:
        root = sh.toplevel(t)  # the worktree or checkout the record lives in
        rel = os.path.relpath(os.path.realpath(t), os.path.realpath(root)).replace(os.sep, "/").casefold()
        if any(rel == d or rel.startswith(d + "/") for d in DECISION_DIRS):
            hits.append(t)
    return hits


def human_only(data, segs):
    tool = data["tool_name"]
    if BROWSER_MCP.search(tool) or tool in ("computer", "Computer"):
        if _consume_grant("browser", data):
            return None
        return ("operating a browser or the desktop acts as the signed-in operator, a human-only act. The operator can "
                "grant one act with `abacus-authorize browser` in their own terminal.")
    if tool.startswith("mcp__") and "__" in tool[5:] and shared_write_mcp(tool.rsplit("__", 1)[1]):
        return (f"{tool.rsplit('__', 1)[1]} changes a shared system (a tracker, a page, a repository) as the signed-in "
                f"operator, a human-only act. Read through MCP; say what should change and the operator makes it.")
    if tool in MESSAGE_TOOLS:
        msg = (data.get("tool_input") or {}).get("message") or ""
        bad = _unattested(RULED.findall(msg if isinstance(msg, str) else json.dumps(msg)), data)
        if bad:
            return (f'this message quotes the operator ("{bad[:80]}") but those words are not in any operator turn '
                    f'of this session or the logbook. Never put words in the operator\'s mouth; ask them.')
        return None
    for s in segs:
        if s.cmd == "abacus-authorize":
            return "abacus-authorize is the operator's command; agents never grant themselves authority"
        if s.cmd == "abacus-logbook" and s.argv[1:3] == ["ruling", "add"]:
            w = next((s.argv[i + 1] for i, a in enumerate(s.argv[:-1]) if a == "--words"), None)
            w = w or next((a.split("=", 1)[1] for a in s.argv if a.startswith("--words=")), None)
            bad = _unattested([_expand_vars(w, s.env)] if w else [], data)
            if bad:
                return (f'"{bad[:80]}" is not in any operator turn of this session, so it cannot be recorded as the '
                        f"operator's ruling. Ask the operator to state it here, then record it verbatim.")
        if s.cmd == "bd":
            bad = _unattested(operator_quotes(_expand_vars(" ".join(s.argv), s.env)), data)
            if bad:
                return (f'this bead note quotes the operator ("{bad[:80]}") but those words were never said by the '
                        f"operator in this session or recorded in the logbook")
        if s.cmd == "open" and any(re.match(r"^(https?|file|ftp)://", a) or BROWSER_APPS.search(a)
                                   for a in s.argv[1:]):
            if not _consume_grant("browser", data):
                return "opening a browser acts as the signed-in operator (human-only; `abacus-authorize browser`)"
        if s.cmd == "osascript":
            script = " ".join(s.argv[1:])
            if re.search(r"System Events|keystroke|click", script, re.I) or \
                    re.search(r'tell application "[^"]*(' + BROWSER_APPS.pattern + ')', script, re.I):
                if not _consume_grant("browser", data):
                    return "GUI or browser control through AppleScript is human-only (`abacus-authorize browser`)"
        if any(REAL_PROFILE.search(a) for a in s.argv[1:]) and not _consume_grant("browser", data):
            return "driving a browser with the operator's real profile acts as the signed-in operator (human-only)"
    if tool in FILE_TOOLS or tool == "Bash":
        hits = _decision_targets(data, segs)
        if hits and not _consume_grant("decision", data):
            return (f"{hits[0]} is a decision record; publishing, editing or deleting decisions is human-only. The "
                    f"operator can grant one change with `abacus-authorize decision` in their own terminal.")
    return None


GUARDS = {"repo-fence": repo_fence, "role-policy": role_policy, "role-commands": role_commands,
          "foreign-bd": foreign_bd,
          "destructive-git": destructive_git, "stage-writes": stage_writes, "database-url": database_url,
          "operator-only": operator_only, "secrets": secrets, "human-only": human_only}
BASH_ONLY = {"foreign-bd", "destructive-git", "stage-writes", "database-url", "operator-only", "role-commands"}
READ_GUARDS = {"secrets"}  # the only guard that inspects Read/Grep calls


def main(names, data):
    unknown = [n for n in names if n not in GUARDS]
    if unknown:
        sh.refuse("abacus-guards", f"unknown guard(s) {unknown} wired in settings (fail closed)", data)
    tool = data.get("tool_name")
    other = tool not in FILE_TOOLS and tool not in READ_TOOLS and tool != "Bash"  # messages, MCP, computer use
    segs = bash_segments(data) if tool == "Bash" else []
    for n in names:
        if other and n != "human-only":
            continue
        if n in BASH_ONLY and tool != "Bash":
            continue
        if tool in READ_TOOLS and n not in READ_GUARDS:
            continue
        reason = GUARDS[n](data, segs)
        if reason:
            if n in ("repo-fence", "role-policy") and interpreter_one_liner(segs):
                reason += INTERPRETER_HINT
            sh.refuse(n, reason, data)
    return 0


INTERPRETER_HINT = (" (This was an interpreter one-liner. The fence can't tell a script's read from a write, so it "
                    "treats it as a write. If you only meant to read, use cat, head, grep or sed -n instead.)")


def interpreter_one_liner(segs):
    for s in segs:
        if s.cmd in INTERPRETERS and INTERPRETERS[s.cmd] in s.argv:
            i = s.argv.index(INTERPRETERS[s.cmd])
            if WRITE_API.search(s.argv[i + 1] if i + 1 < len(s.argv) else ""):
                return True
    return False


def run(names):
    """Fail closed on a crash too: Claude Code reads exit 1 as non-blocking, so an uncaught exception would allow."""
    try:
        data = sh.payload()
        try:
            return main(names, data)
        except Exception as e:
            sh.refuse("abacus-guards", f"guard crashed: {e!r} (fail closed)", data)
    except SystemExit:
        raise  # refuse() already exits 2
    except BaseException as e:  # a crash in the payload read or in refuse() itself still blocks
        reason = f"guard crashed: {e!r} (fail closed)"
        try:
            sh.log_refusal("abacus-guards", reason, {})
        except BaseException:
            pass
        try:
            print(f"[abacus:abacus-guards] REFUSED: {reason}", file=sys.stderr)
        except BaseException:
            pass
        return 2


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
