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
  set(PROMEKI_MIRRORS
      "https://github.com/some/repo.git"
          "ssh://git@gitlab.example.com:22/mirror-group/repo.git"
      "https://github.com/other/repo.git"
          "ssh://git@gitlab.other.com/mirror-group/repo.git"
      ...
  )

  # Optional: per-host GitLab API URL overrides.  When a mirror push URL
  # references a host not listed here, the API base defaults to
  # `https://<host>`, which works for any GitLab on the standard HTTPS port.
  set(PROMEKI_MIRROR_APIS
      "gitlab.example.com"  "https://gitlab.example.com:8443"
      ...
  )

  # Legacy single-API form (still accepted): treated as an entry in
  # PROMEKI_MIRROR_APIS for the host implied by the URL itself.
  set(PROMEKI_MIRROR_API "https://gitlab.example.com")

PROMEKI_MIRROR_API / PROMEKI_MIRROR_APIS are consumed only by this script
(CMake ignores them).  PROMEKI_MIRRORS entries that don't exactly match a
submodule upstream URL are also ignored by this script — they may still
be used by CMake (e.g. prefix rewrites).

Auth
----
GitLab API tokens (required for existence-check / auto-create) are looked
up per host.  For host `H`, the search order is:
  1. $GITLAB_TOKEN_<H>   env var; `H` uppercased with every non-alphanumeric
                         character replaced by `_`, so the token for
                         `git.howardlogic.com` lives in
                         $GITLAB_TOKEN_GIT_HOWARDLOGIC_COM
  2. $GITLAB_TOKEN       env var (generic fallback for any host)
  3. ~/.config/promeki/gitlab-tokens   multi-line file (chmod 600); one
                         entry per line as `host token` or `host=token`,
                         `#` line comments allowed
  4. ~/.config/promeki/gitlab-token-<host>   single-line per-host file
  5. ~/.config/promeki/gitlab-token   single-line generic fallback file
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


def _host_env_suffix(host: str) -> str:
    """Convert a hostname to an env-var-safe uppercase suffix.

    `git.howardlogic.com` -> `GIT_HOWARDLOGIC_COM`
    """
    return re.sub(r"[^A-Za-z0-9]", "_", host).upper()


# Paths we've already complained about (so a single tokens file gets one
# warning even when load_token_for_host() is called once per host).
_warned_perm_paths: set[Path] = set()


def _check_token_file_perms(path: Path) -> None:
    """Warn (once per path) if a token file is group- or world-accessible.

    POSIX only — Windows ACLs don't map to a sensible mode check, and the
    Python stdlib's `st_mode` on Windows reports only the read-only bit.
    """
    if sys.platform == "win32":
        return
    if path in _warned_perm_paths:
        return
    try:
        st = path.stat()
    except OSError:
        return
    mode = st.st_mode & 0o777
    if mode & 0o077:
        _warned_perm_paths.add(path)
        info(
            f"warning: {path} has permissions {mode:04o}; tokens are "
            f"readable by group/other. Run `chmod 600 {path}` to fix."
        )


def _parse_tokens_file(path: Path) -> dict[str, str]:
    """Parse a multi-line `host token` / `host=token` file.

    Blank lines and `#` line comments are ignored.  Later entries for the
    same host override earlier ones.
    """
    result: dict[str, str] = {}
    if not path.exists():
        return result
    _check_token_file_perms(path)
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # Prefer `=` split if present; otherwise split on whitespace.
        # GitLab tokens (`glpat-...`) never contain `=`.
        if "=" in line:
            host, _, tok = line.partition("=")
        else:
            parts = line.split(None, 1)
            if len(parts) != 2:
                continue
            host, tok = parts
        host = host.strip()
        tok = tok.strip()
        if host and tok:
            result[host] = tok
    return result


def load_token_for_host(host: str) -> Optional[str]:
    """Look up a GitLab API token for `host`.  See module docstring for
    the full search order."""
    env_specific = f"GITLAB_TOKEN_{_host_env_suffix(host)}"
    tok = os.environ.get(env_specific)
    if tok and tok.strip():
        return tok.strip()
    tok = os.environ.get("GITLAB_TOKEN")
    if tok and tok.strip():
        return tok.strip()

    config_dir = Path.home() / ".config" / "promeki"
    multi = _parse_tokens_file(config_dir / "gitlab-tokens")
    if host in multi:
        return multi[host]

    per_host = config_dir / f"gitlab-token-{host}"
    if per_host.exists():
        _check_token_file_perms(per_host)
        t = per_host.read_text().strip()
        if t:
            return t

    legacy = config_dir / "gitlab-token"
    if legacy.exists():
        _check_token_file_perms(legacy)
        t = legacy.read_text().strip()
        if t:
            return t

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


def parse_push_url(push_url: str) -> tuple[str, str, str]:
    """Given a mirror push URL, return (host, group_path, project_name).
    Supports `ssh://user@host[:port]/group/.../name[.git]` and the SCP
    syntax `user@host:group/.../name[.git]`."""
    host: str
    rest: str
    m = re.match(r"^ssh://(?:[^@/]+@)?([^/:]+)(?::\d+)?/(.+)$", push_url)
    if m:
        host = m.group(1)
        rest = m.group(2)
    else:
        # SCP-like form: user@host:path
        m = re.match(r"^(?:[^@/]+@)?([^:/]+):(.+)$", push_url)
        if not m:
            die(f"can't parse push URL: {push_url}")
        host = m.group(1)
        rest = m.group(2)
    if rest.endswith(".git"):
        rest = rest[:-4]
    rest = rest.strip("/")
    if "/" not in rest:
        die(f"push URL has no namespace: {push_url}")
    group, _, name = rest.rpartition("/")
    return host, group, name


def _api_url_host(url: str) -> Optional[str]:
    """Extract the hostname from an `http[s]://host[:port]/...` URL."""
    m = re.match(r"^https?://([^/:]+)", url)
    return m.group(1) if m else None


def build_api_map(
    config: dict[str, Union[str, list[str]]], path: Path
) -> dict[str, str]:
    """Build the host -> GitLab-API-base-URL map.

    Combines PROMEKI_MIRROR_APIS (the new pairs-list form) with the
    legacy PROMEKI_MIRROR_API single-URL form.  The legacy entry is
    folded in under the host implied by its own URL.  Hosts that appear
    in neither fall back to `https://<host>` at lookup time.
    """
    api_map: dict[str, str] = {}

    apis = config.get("PROMEKI_MIRROR_APIS")
    if apis is not None:
        if isinstance(apis, str):
            die(
                f"PROMEKI_MIRROR_APIS in {path} must be a flat list of "
                f"(host, api-url) pairs"
            )
        if len(apis) % 2 != 0:
            die(
                f"PROMEKI_MIRROR_APIS in {path} must be a flat list of "
                f"(host, api-url) pairs; got {len(apis)} entries"
            )
        for i in range(0, len(apis), 2):
            api_map[apis[i]] = apis[i + 1]

    legacy = config.get("PROMEKI_MIRROR_API")
    if isinstance(legacy, str) and legacy:
        host = _api_url_host(legacy)
        if not host:
            die(f"PROMEKI_MIRROR_API in {path} is not a valid http(s) URL: {legacy}")
        api_map.setdefault(host, legacy)

    return api_map


def api_url_for_host(host: str, api_map: dict[str, str]) -> str:
    """Look up the GitLab API base URL for `host`.  Defaults to
    `https://<host>` when no explicit override is configured."""
    return api_map.get(host, f"https://{host}")


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
        help="path to a CMake mirrors file defining PROMEKI_MIRRORS (and "
             "optionally PROMEKI_MIRROR_APIS / PROMEKI_MIRROR_API); if "
             "omitted, the script searches well-known locations (see the "
             "module docstring or --help epilog)",
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
    mirror_map = build_mirror_map(config, config_path)
    api_map = build_api_map(config, config_path)

    # Per-host GitLab client cache + a set of hosts we've already warned
    # about (so the "no token" notice fires at most once per host).
    clients: dict[str, GitLab] = {}
    warned_hosts: set[str] = set()

    def client_for(host: str) -> GitLab:
        cached = clients.get(host)
        if cached is not None:
            return cached
        api = api_url_for_host(host, api_map)
        tok = load_token_for_host(host)
        if not tok and host not in warned_hosts:
            info(
                f"warning: no GitLab token found for {host}; skipping API "
                f"checks for this host (set $GITLAB_TOKEN_{_host_env_suffix(host)} "
                f"or add an entry in ~/.config/promeki/gitlab-tokens to enable "
                f"existence checks and auto-create)"
            )
            warned_hosts.add(host)
        gl = GitLab(api, tok)
        clients[host] = gl
        return gl

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

        host, group, project_name = parse_push_url(push_url)
        full_path = f"{group}/{project_name}"
        gl = client_for(host)

        info(f"\n=== {path} ===")
        info(f"  upstream: {upstream}")
        info(f"  mirror:   {gl.base}/{full_path}")

        if push_url in completed_pushes:
            info(f"  status:   shares mirror with {completed_pushes[push_url]}; skipping (already done this run)")
            shared.append(basename)
            continue

        if gl.token:
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
