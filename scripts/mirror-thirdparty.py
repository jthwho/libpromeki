#!/usr/bin/env python3
"""Mirror libpromeki thirdparty submodules to a self-hosted GitLab.

All mirror locations come from a configuration file (see --config).  The
file is a small subset of CMake — the same one consumed by
`-DPROMEKI_MIRRORS_FILE=` at build time — so a single file drives both
build-time submodule fetch overrides and this maintenance script.

For each submodule listed in .gitmodules this script:
  1. Looks up the submodule's upstream URL in PROMEKI_MIRRORS to find the
     configured push target.  No entry => submodule is skipped.
  2. (With a GitLab API token) checks whether the project exists at the
     URL implied by the push target; if missing, creates it.  If it
     already exists, no creation is performed.
  3. Maintains a bare cache clone, fetching the latest from upstream.
  4. Runs `git push --mirror` to the GitLab project.  This is the update
     step: it brings the mirror's refs into agreement with upstream
     whether the project was just created or already existed.

Config file (CMake syntax)
--------------------------
  set(PROMEKI_MIRROR_API "https://gitlab.example.com")
  set(PROMEKI_MIRRORS
      "https://github.com/some/repo.git"
          "ssh://git@gitlab.example.com:22/mirror-group/repo.git"
      ...
  )

PROMEKI_MIRROR_API is consumed only by this script (CMake ignores it).
PROMEKI_MIRRORS entries that don't exactly match a submodule upstream URL
are also ignored by this script — they may still be used by CMake (e.g.
prefix rewrites).

Auth
----
GitLab API token (required for existence-check / auto-create):
  - $GITLAB_TOKEN              env var, or
  - ~/.config/promeki/gitlab-token   single-line file (chmod 600)
The token needs `api` scope.  SSH push uses your existing agent / keys.

Config discovery
----------------
If --config is omitted, the script searches a small set of well-known
locations and uses the first one that exists.  This matches the search
list in cmake/PromekiSubmodules.cmake so a single file drives both CMake
submodule fetches and this script.

Search order (first hit wins):
  1. $PROMEKI_MIRRORS_FILE                                  (env var)
  2. <repo>/mirrors.cmake                                   (gitignored)
  3. Per-user config dir:
       Linux:   $XDG_CONFIG_HOME/promeki/mirrors.cmake
                  (defaulting to ~/.config/promeki/mirrors.cmake)
       macOS:   ~/Library/Application Support/promeki/mirrors.cmake
                  ~/.config/promeki/mirrors.cmake             (XDG fallback)
       Windows: %APPDATA%/promeki/mirrors.cmake
  4. System-wide config:
       Linux:   /etc/promeki/mirrors.cmake
       macOS:   /Library/Application Support/promeki/mirrors.cmake
                  /etc/promeki/mirrors.cmake
       Windows: %PROGRAMDATA%/promeki/mirrors.cmake

Usage
-----
  scripts/mirror-thirdparty.py                              # auto-discover config
  scripts/mirror-thirdparty.py --config path/to/mirrors.cmake
  scripts/mirror-thirdparty.py --dry-run
  scripts/mirror-thirdparty.py --only cirf,libvtc
  scripts/mirror-thirdparty.py --no-create

Exit codes: 0 all good, 1 setup error, 2 one or more repos failed.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Optional, Union

REPO_ROOT = Path(__file__).resolve().parent.parent


def well_known_config_paths() -> list[Path]:
    """Ordered list of well-known mirror-config paths to probe.

    Kept in lockstep with `_promeki_find_mirrors_file` in
    cmake/PromekiSubmodules.cmake.
    """
    paths: list[Path] = []

    env = os.environ.get("PROMEKI_MIRRORS_FILE", "").strip()
    if env:
        paths.append(Path(env))

    paths.append(REPO_ROOT / "mirrors.cmake")

    if sys.platform == "win32":
        appdata = os.environ.get("APPDATA", "").strip()
        if appdata:
            paths.append(Path(appdata) / "promeki" / "mirrors.cmake")
        programdata = os.environ.get("PROGRAMDATA", "").strip()
        if programdata:
            paths.append(Path(programdata) / "promeki" / "mirrors.cmake")
    elif sys.platform == "darwin":
        try:
            home = Path.home()
        except RuntimeError:
            home = None
        if home is not None:
            paths.append(home / "Library" / "Application Support" / "promeki" / "mirrors.cmake")
            paths.append(home / ".config" / "promeki" / "mirrors.cmake")
        paths.append(Path("/Library/Application Support/promeki/mirrors.cmake"))
        paths.append(Path("/etc/promeki/mirrors.cmake"))
    else:
        xdg = os.environ.get("XDG_CONFIG_HOME", "").strip()
        if xdg:
            paths.append(Path(xdg) / "promeki" / "mirrors.cmake")
        else:
            try:
                home = Path.home()
            except RuntimeError:
                home = None
            if home is not None:
                paths.append(home / ".config" / "promeki" / "mirrors.cmake")
        paths.append(Path("/etc/promeki/mirrors.cmake"))

    return paths


def discover_config_path() -> Optional[Path]:
    """First well-known config path that exists on disk, or None."""
    for p in well_known_config_paths():
        if p.is_file():
            return p
    return None


def default_cache() -> Path:
    """Default location for the bare-clone cache.

    Honors $PROMEKI_OPT_TempDir (the same env var that overrides
    `Dir::temp()` in the library — see LibraryOptions::TempDir).  When
    unset, falls back to the OS default temp directory under a
    `promeki/` subdir.
    """
    base = os.environ.get("PROMEKI_OPT_TempDir", "").strip()
    if base:
        return Path(base) / "mirror-cache"
    return Path(tempfile.gettempdir()) / "promeki" / "mirror-cache"


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def info(msg: str = "") -> None:
    print(msg, flush=True)


def load_token() -> Optional[str]:
    tok = os.environ.get("GITLAB_TOKEN")
    if tok:
        return tok.strip() or None
    path = Path.home() / ".config" / "promeki" / "gitlab-token"
    if path.exists():
        return path.read_text().strip() or None
    return None


def parse_cmake_config(path: Path) -> dict[str, Union[str, list[str]]]:
    """Parse a small subset of CMake: set(NAME "v1" ["v2" ...]) with
    `${VAR}` substitution and `#` line comments.  Returns a dict mapping
    each variable name to either a single string or a list of strings."""
    if not path.exists():
        die(f"config file not found: {path}")
    raw = path.read_text()

    # Strip line comments (# outside of double-quoted strings).
    cleaned: list[str] = []
    for line in raw.splitlines():
        in_str = False
        cut = len(line)
        for i, ch in enumerate(line):
            if ch == '"':
                in_str = not in_str
            elif ch == "#" and not in_str:
                cut = i
                break
        cleaned.append(line[:cut])
    text = "\n".join(cleaned)

    config: dict[str, Union[str, list[str]]] = {}

    def substitute(value: str) -> str:
        def repl(m: "re.Match[str]") -> str:
            var = m.group(1)
            v = config.get(var, "")
            if isinstance(v, str):
                return v
            return ""
        return re.sub(r"\$\{(\w+)\}", repl, value)

    for m in re.finditer(r"\bset\s*\(\s*(\w+)\s+(.*?)\)", text, re.DOTALL):
        name = m.group(1)
        body = m.group(2)
        # Pull out double-quoted tokens (the only form we support).
        tokens = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        tokens = [substitute(t) for t in tokens]
        if not tokens:
            continue
        config[name] = tokens[0] if len(tokens) == 1 else tokens
    return config


def list_submodules() -> list[tuple[str, str]]:
    """Returns a list of (submodule-path, upstream-url) pairs from .gitmodules."""
    raw = subprocess.check_output(
        [
            "git",
            "config",
            "--file",
            str(REPO_ROOT / ".gitmodules"),
            "--get-regexp",
            r"^submodule\..+\.url$",
        ],
        text=True,
    )
    mods: list[tuple[str, str]] = []
    for line in raw.splitlines():
        line = line.strip()
        if not line:
            continue
        key, _, url = line.partition(" ")
        if not key.startswith("submodule.") or not key.endswith(".url"):
            continue
        path = key[len("submodule.") : -len(".url")]
        mods.append((path, url))
    return mods


def parse_push_url(push_url: str) -> tuple[str, str]:
    """Given a mirror push URL, return (group_path, project_name).
    Supports `ssh://user@host[:port]/group/.../name[.git]` and the SCP
    syntax `user@host:group/.../name[.git]`."""
    rest: str
    m = re.match(r"^ssh://(?:[^@/]+@)?[^/]+/(.+)$", push_url)
    if m:
        rest = m.group(1)
    else:
        # SCP-like form: user@host:path
        m = re.match(r"^(?:[^@/]+@)?[^:/]+:(.+)$", push_url)
        if not m:
            die(f"can't parse push URL: {push_url}")
        rest = m.group(1)
    if rest.endswith(".git"):
        rest = rest[:-4]
    rest = rest.strip("/")
    if "/" not in rest:
        die(f"push URL has no namespace: {push_url}")
    group, _, name = rest.rpartition("/")
    return group, name


class GitLab:
    def __init__(self, base_url: str, token: Optional[str]):
        self.base = base_url.rstrip("/")
        self.token = token
        self._group_cache: dict[str, int] = {}

    def _request(self, method: str, path: str, data=None):
        url = self.base + path
        body = None
        headers = {"Accept": "application/json"}
        if data is not None:
            body = json.dumps(data).encode()
            headers["Content-Type"] = "application/json"
        if self.token:
            headers["PRIVATE-TOKEN"] = self.token
        req = urllib.request.Request(url, data=body, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req) as resp:
                raw = resp.read()
                if not raw:
                    return {}
                return json.loads(raw.decode())
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            detail = e.read().decode(errors="replace")
            die(
                f"GitLab API {method} {path} failed: HTTP {e.code} {e.reason}\n{detail}"
            )
        except urllib.error.URLError as e:
            die(f"GitLab API {method} {path} failed: {e.reason}")

    def get_project(self, full_path: str):
        encoded = urllib.parse.quote(full_path, safe="")
        return self._request("GET", f"/api/v4/projects/{encoded}")

    def get_group_id(self, group_path: str) -> int:
        if group_path in self._group_cache:
            return self._group_cache[group_path]
        encoded = urllib.parse.quote(group_path, safe="")
        group = self._request("GET", f"/api/v4/groups/{encoded}")
        if not group:
            die(
                f"GitLab group `{group_path}` not found at {self.base}/{group_path}; "
                f"create it first or fix the mirror URL in the config"
            )
        self._group_cache[group_path] = group["id"]
        return group["id"]

    def create_project(self, group: str, name: str, description: str):
        ns_id = self.get_group_id(group)
        return self._request(
            "POST",
            "/api/v4/projects",
            data={
                "name": name,
                "path": name,
                "namespace_id": ns_id,
                "visibility": "private",
                "default_branch": "main",
                "description": description,
                "lfs_enabled": False,
                "issues_access_level": "disabled",
                "merge_requests_access_level": "disabled",
                "wiki_access_level": "disabled",
                "snippets_access_level": "disabled",
                "packages_enabled": False,
            },
        )


def run(cmd, **kw):
    kw.setdefault("check", True)
    return subprocess.run(cmd, **kw)


# Ref namespaces that GitLab refuses to accept ("hidden refs"), or that
# we don't want to propagate into a mirror anyway:
#   - refs/remotes/*       local remote-tracking refs; never push these
#   - refs/pull/*          GitHub's per-PR refs
#   - refs/merge-requests/* GitLab's per-MR refs (e.g. gitlab.freedesktop.org)
#   - refs/keep-around/*   GitLab-internal anchors
#   - refs/pipelines/*     GitLab CI refs
#   - refs/environments/*  GitLab environment refs
#   - refs/tmp/*           any throwaway namespace some hosts populate
_UNSAFE_REF_PREFIXES = (
    "refs/remotes/",
    "refs/pull/",
    "refs/merge-requests/",
    "refs/keep-around/",
    "refs/pipelines/",
    "refs/environments/",
    "refs/tmp/",
)


def _configure_safe_origin(bare: Path, upstream: str) -> None:
    """Reset `origin` to fetch only branches + tags.

    Bare clones made with `git clone --mirror` set `remote.origin.mirror = true`
    and a catch-all `+refs/*:refs/*` refspec, which drags PR / MR / remote-
    tracking refs into the cache.  Our mirror push won't touch those (see
    `_UNSAFE_REF_PREFIXES`) but a safer cache means less wasted disk and
    fewer surprises down the line.
    """
    run(["git", "-C", str(bare), "config", "remote.origin.url", upstream])
    run(
        ["git", "-C", str(bare), "config", "--unset-all", "remote.origin.mirror"],
        check=False,
    )
    run(
        ["git", "-C", str(bare), "config", "--unset-all", "remote.origin.tagopt"],
        check=False,
    )
    run(
        ["git", "-C", str(bare), "config", "--unset-all", "remote.origin.fetch"],
        check=False,
    )
    run(
        ["git", "-C", str(bare), "config", "--add", "remote.origin.fetch",
         "+refs/heads/*:refs/heads/*"]
    )
    run(
        ["git", "-C", str(bare), "config", "--add", "remote.origin.fetch",
         "+refs/tags/*:refs/tags/*"]
    )


def _purge_unsafe_refs(bare: Path) -> int:
    """Delete refs in namespaces we don't mirror.  Returns count removed."""
    out = subprocess.check_output(
        ["git", "-C", str(bare), "for-each-ref", "--format=%(refname)"],
        text=True,
    )
    targets = [
        r for r in out.split()
        if any(r.startswith(p) for p in _UNSAFE_REF_PREFIXES)
    ]
    for ref in targets:
        run(["git", "-C", str(bare), "update-ref", "-d", ref])
    return len(targets)


def ensure_cache(cache_dir: Path, upstream: str, basename: str) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    bare = cache_dir / f"{basename}.git"
    if not bare.exists():
        info(f"  clone --bare: {upstream}")
        run(["git", "clone", "--bare", upstream, str(bare)])
    _configure_safe_origin(bare, upstream)
    purged = _purge_unsafe_refs(bare)
    if purged:
        info(f"  purged {purged} unsafe local ref(s)")
    info(f"  fetch: {upstream}")
    run(["git", "-C", str(bare), "fetch", "--prune", "origin"])
    return bare


def _ensure_mirror_remote(bare: Path, mirror_ssh: str) -> None:
    remotes = subprocess.check_output(
        ["git", "-C", str(bare), "remote"], text=True
    ).split()
    if "mirror" in remotes:
        run(["git", "-C", str(bare), "remote", "set-url", "mirror", mirror_ssh])
    else:
        run(["git", "-C", str(bare), "remote", "add", "mirror", mirror_ssh])


def push_mirror(bare: Path, mirror_ssh: str) -> None:
    """Push branches and tags to the mirror.

    Uses explicit refspecs (not `git push --mirror`) so that:
      - we never attempt to push refs in `_UNSAFE_REF_PREFIXES`, which
        GitLab rejects with "deny updating a hidden ref";
      - `--prune` only prunes branches and tags that disappeared upstream,
        leaving GitLab's own internal refs (refs/keep-around/*, etc.)
        untouched.
    """
    info(f"  push -> {mirror_ssh}")
    _ensure_mirror_remote(bare, mirror_ssh)
    run(
        [
            "git", "-C", str(bare), "push",
            "--prune", "--force",
            "mirror",
            "refs/heads/*:refs/heads/*",
            "refs/tags/*:refs/tags/*",
        ]
    )


def clean_remote_mirror(bare: Path, mirror_ssh: str, dry_run: bool) -> None:
    """Delete refs in `_UNSAFE_REF_PREFIXES` from the mirror.

    These refs can accumulate on a mirror from a previous `git push --mirror`
    run that uploaded GitHub PR refs (refs/pull/*) or GitLab MR refs
    (refs/merge-requests/*).  They don't hurt anything functionally, but
    they waste disk on the mirror server.  Deletes are batched into one
    push per ~200 refs to keep argv reasonable.  Refs in namespaces the
    destination won't allow (e.g. refs/remotes/* on GitLab) silently
    remain; the count of unremoved refs is reported.
    """
    _ensure_mirror_remote(bare, mirror_ssh)
    info("  scan remote for unsafe refs")
    ls = subprocess.check_output(
        ["git", "-C", str(bare), "ls-remote", "mirror"], text=True
    )
    targets: list[str] = []
    for line in ls.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        ref = parts[1]
        if any(ref.startswith(p) for p in _UNSAFE_REF_PREFIXES):
            targets.append(ref)
    if not targets:
        info("  remote-clean: no unsafe refs found")
        return
    if dry_run:
        info(f"  remote-clean: would delete {len(targets)} unsafe ref(s) (dry-run)")
        return
    info(f"  remote-clean: deleting {len(targets)} unsafe ref(s)")
    BATCH = 200
    for i in range(0, len(targets), BATCH):
        batch = targets[i : i + BATCH]
        # Empty source means delete on the remote.
        specs = [f":{r}" for r in batch]
        subprocess.run(
            ["git", "-C", str(bare), "push", "mirror", *specs],
            capture_output=True, text=True,
        )
    # Re-scan to see what (if anything) couldn't be removed.
    ls2 = subprocess.check_output(
        ["git", "-C", str(bare), "ls-remote", "mirror"], text=True
    )
    remaining = 0
    for line in ls2.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        if any(parts[1].startswith(p) for p in _UNSAFE_REF_PREFIXES):
            remaining += 1
    deleted = len(targets) - remaining
    info(f"  remote-clean: removed {deleted} ref(s)")
    if remaining > 0:
        info(f"  remote-clean: {remaining} ref(s) refused by remote (left in place)")


def build_mirror_map(config: dict[str, Union[str, list[str]]], path: Path) -> dict[str, str]:
    mirrors = config.get("PROMEKI_MIRRORS")
    if mirrors is None:
        die(f"PROMEKI_MIRRORS not set in {path}")
    if isinstance(mirrors, str):
        mirrors = [mirrors]
    if len(mirrors) % 2 != 0:
        die(
            f"PROMEKI_MIRRORS in {path} must be a flat list of (upstream, mirror) "
            f"pairs; got {len(mirrors)} entries"
        )
    return {mirrors[i]: mirrors[i + 1] for i in range(0, len(mirrors), 2)}


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument(
        "--config",
        type=Path,
        default=None,
        help="path to a CMake mirrors file defining PROMEKI_MIRROR_API and "
             "PROMEKI_MIRRORS; if omitted, the script searches well-known "
             "locations (see the module docstring or --help epilog)",
    )
    ap.add_argument(
        "--cache",
        default=default_cache(),
        type=Path,
        help="bare-clone cache directory (default: $PROMEKI_OPT_TempDir/mirror-cache, "
             "or <system temp>/promeki/mirror-cache if unset)",
    )
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument(
        "--no-create",
        action="store_true",
        help="don't auto-create missing GitLab projects (report and skip instead)",
    )
    ap.add_argument(
        "--only",
        action="append",
        default=[],
        help="limit to specific submodules by basename; repeatable or comma-separated",
    )
    ap.add_argument(
        "--clean-remote",
        action="store_true",
        help="after pushing each mirror, delete any refs on the remote in "
             "unsafe namespaces (refs/pull/*, refs/merge-requests/*, etc.) "
             "that earlier `git push --mirror` runs may have uploaded",
    )
    args = ap.parse_args()

    only_set: set[str] = set()
    for spec in args.only:
        only_set.update(s.strip() for s in spec.split(",") if s.strip())

    config_path: Optional[Path] = args.config
    if config_path is None:
        config_path = discover_config_path()
        if config_path is None:
            tried = "\n  ".join(str(p) for p in well_known_config_paths())
            die(
                "no --config provided and no mirror config found in any "
                f"well-known location. Searched:\n  {tried}"
            )
        info(f"using auto-discovered config: {config_path}")

    config = parse_cmake_config(config_path)
    api_url = config.get("PROMEKI_MIRROR_API")
    if not isinstance(api_url, str) or not api_url:
        die(f"PROMEKI_MIRROR_API not set in {config_path}")
    mirror_map = build_mirror_map(config, config_path)

    token = load_token()
    if not token:
        info(
            "warning: no GitLab token found; skipping API checks "
            "(set GITLAB_TOKEN or ~/.config/promeki/gitlab-token to enable existence "
            "checks and auto-create)"
        )

    gl = GitLab(api_url, token)

    mods = list_submodules()
    if not mods:
        die("no submodules found in .gitmodules")

    successes: list[str] = []
    created: list[str] = []
    skipped: list[str] = []
    shared: list[str] = []
    failures: list[tuple[str, str]] = []
    # Mirror URLs that have already had their fetch + push performed in
    # this run.  Two submodules resolving to the same mirror URL (e.g.
    # mbedtls + srt-mbedtls both pointing at upstream Mbed-TLS/mbedtls)
    # share the same project on the GitLab server, so the second one is
    # a redundant no-op.
    completed_pushes: dict[str, str] = {}

    for path, upstream in mods:
        basename = path.rsplit("/", 1)[-1]
        if only_set and basename not in only_set:
            continue

        push_url = mirror_map.get(upstream)
        if push_url is None:
            info(f"\n=== {path} ===")
            info(f"  upstream: {upstream}")
            info(f"  status:   no mirror entry in config; skipping")
            skipped.append(basename)
            continue

        group, project_name = parse_push_url(push_url)
        full_path = f"{group}/{project_name}"

        info(f"\n=== {path} ===")
        info(f"  upstream: {upstream}")
        info(f"  mirror:   {api_url}/{full_path}")

        if push_url in completed_pushes:
            info(f"  status:   shares mirror with {completed_pushes[push_url]}; skipping (already done this run)")
            shared.append(basename)
            continue

        if token:
            project = gl.get_project(full_path)
            if project is None:
                if args.no_create:
                    info("  status:   missing (--no-create; skipping)")
                    skipped.append(basename)
                    continue
                if args.dry_run:
                    info("  status:   would create")
                    created.append(full_path)
                else:
                    info("  status:   creating project")
                    gl.create_project(
                        group,
                        project_name,
                        description=f"Mirror of {upstream}",
                    )
                    created.append(full_path)
            else:
                info(f"  status:   exists ({project.get('web_url', '')})")

        if args.dry_run:
            info("  action:   would fetch upstream + push (dry-run)")
            if args.clean_remote:
                info("  action:   would clean remote unsafe refs (dry-run)")
            successes.append(basename)
            completed_pushes[push_url] = path
            continue

        try:
            bare = ensure_cache(args.cache, upstream, basename)
            push_mirror(bare, push_url)
            if args.clean_remote:
                clean_remote_mirror(bare, push_url, args.dry_run)
            successes.append(basename)
            completed_pushes[push_url] = path
        except subprocess.CalledProcessError as e:
            failures.append((basename, f"git exited {e.returncode}"))
            info(f"  status:   FAILED (git exit {e.returncode})")

    info("\n=== summary ===")
    info(f"  succeeded: {len(successes)}")
    if shared:
        info(f"  shared:    {len(shared)} ({', '.join(shared)})")
    if created:
        verb = "would create" if args.dry_run else "created"
        info(f"  {verb}:   {len(created)} ({', '.join(created)})")
    if skipped:
        info(f"  skipped:   {len(skipped)} ({', '.join(skipped)})")
    if failures:
        info(f"  failed:    {len(failures)}")
        for name, why in failures:
            info(f"    - {name}: {why}")
        sys.exit(2)


if __name__ == "__main__":
    main()
