"""Fetch the pinned tools setup needs, and check each by size and SHA-256.

toolchain.json names each tool at one exact version: its URL, size and
SHA-256. An entry without a SHA-256 is not pinned yet and is refused, so
nothing unchecked is ever used. Downloads resume from where they stopped
(HTTP Range), go to a .part file and are renamed only once they match.
Archives (.zip, and on Linux .tar.xz and .tar.gz, docs/87) are unpacked into a
folder of their own, refusing any entry that would land outside it and any
link.

On Windows, when Python's own check of a server's certificate fails, the
download goes through Windows' own curl.exe instead (windows_curl, below).

Nothing is installed: everything stays in setup's data folder.
"""
import hashlib
import json
import os
import posixpath
import shutil
import ssl
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

MANIFEST = Path(__file__).with_name("toolchain.json")
CHUNK = 1 << 20


class FetchError(RuntimeError):
    pass


# ---- Windows' own TLS, for a server whose certificate Python's check doesn't accept ----
#
# On Windows, Python checks a server's certificate against the root certificates it copies from Windows' store. But
# Windows adds a root to its store only when its own check first needs it (it fetches the root from Windows Update).
# So on a PC where nothing Windows runs has needed that root yet, Python's check fails with "unable to get local
# issuer certificate", where Windows' own check would pass (issue #493: the compiler's download, and the update check,
# on a PC where Windows' curl had just downloaded Python). Windows' own curl.exe (Windows 10 1803 and later) checks
# with Windows' TLS, as the host's Python download does (port/ui/src/setup_python.cpp), so setup downloads through it
# then. The certificate is still checked, and a download still has to match its size and SHA-256.

def cert_failed(err) -> bool:
    """True for a failure of Python's check of a server's certificate (urllib wraps it in a URLError)."""
    return any(isinstance(x, ssl.SSLCertVerificationError) for x in (err, getattr(err, "reason", None)))


def windows_curl():
    """Windows' own curl.exe, by full path from the System folder (never a copy PATH would find first), or None: off
    Windows, without it, or with DES_SETUP_NO_SYSTEM_TOOLS=1 (as the host's Python download has it)."""
    if os.name != "nt" or os.environ.get("DES_SETUP_NO_SYSTEM_TOOLS") == "1":
        return None
    import ctypes
    buf = ctypes.create_unicode_buffer(260)
    n = ctypes.windll.kernel32.GetSystemDirectoryW(buf, len(buf))
    p = Path(buf.value) / "curl.exe" if 0 < n < len(buf) else None
    return p if p and p.is_file() else None


def _curl_args(curl, url, timeout, allow_http) -> list:
    """The host's own curl options (setup_python.cpp): -q first, so no curlrc of the player's changes what it does;
    https only (and the tests' http://127.0.0.1); no meter, only what went wrong; a stalled try ends after `timeout`
    seconds, as urllib's does."""
    proto = "=http,https" if allow_http and url.startswith("http://127.0.0.1") else "=https"
    t = str(max(1, int(timeout)))
    return [str(curl), "-q", "--location", "--proto", proto, "--proto-redir", proto, "--tlsv1.2", "--silent",
            "--show-error", "--connect-timeout", t, "--speed-limit", "1", "--speed-time", t, "--user-agent",
            "des-setup"]


def _run_curl(args, watch=None):
    """curl to its end, with no window; watch() about four times a second meanwhile. (exit code, stdout, curl's own
    error text)."""
    with subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)) as p:
        while True:
            try:
                out, err = p.communicate(timeout=0.25)
                break
            except subprocess.TimeoutExpired:
                if watch:
                    watch()
    return p.returncode, out, err.decode("utf-8", "replace").strip()


def curl_get(url, headers=None, timeout=60, allow_http=False):
    """A small GET through Windows' own curl: (HTTP status, body, the last answer's headers with lower-case names).
    headers: name -> value, sent as they are. Raises OSError when there is no curl or the request does not finish."""
    curl = windows_curl()
    if not curl:
        raise OSError("Windows' own curl.exe is not there")
    with tempfile.TemporaryDirectory(prefix="des-curl-") as tmp:
        body, head = Path(tmp) / "body", Path(tmp) / "head"
        args = _curl_args(curl, url, timeout, allow_http)
        for k, v in (headers or {}).items():
            args += ["--header", f"{k}: {v}"]
        args += ["-o", str(body), "-D", str(head), "-w", "%{http_code}", url]
        rc, out, err = _run_curl(args)
        if rc != 0:
            raise OSError(f"curl exit {rc}" + (f": {err}" if err else ""))
        got = {}
        for line in (head.read_text("latin-1") if head.is_file() else "").splitlines():
            if line.startswith("HTTP/"):   # (each answer's block in turn, redirects first: the last one counts)
                got = {}
            elif ":" in line:
                k, v = line.split(":", 1)
                got[k.strip().lower()] = v.strip()
        code = out.decode("ascii", "replace").strip()
        return int(code) if code.isdigit() else 0, body.read_bytes() if body.is_file() else b"", got


def _curl_fetch(curl, e: dict, part: Path, progress, timeout, allow_http):
    """Entry e's download through Windows' own curl into its .part file, carrying on from what is there (-C -), the
    bar from the file's size (as setup_python.cpp does)."""
    size = e["size"]

    def watch():
        if progress and part.is_file():
            progress(min(part.stat().st_size, size), size)

    for again in (False, True):
        rc, _, err = _run_curl(_curl_args(curl, e["url"], timeout, allow_http) +
                               ["--fail", "-C", "-", "-o", str(part), e["url"]], watch)
        watch()
        if rc == 0:
            return
        if rc == 33 and not again:   # (curl's "cannot resume": the server ignored the range; start over once)
            part.unlink(missing_ok=True)
            continue
        raise FetchError(f"{e['name']}: the download through Windows' curl stopped (curl exit {rc}"
                         + (f": {err}" if err else "") + "); it resumes next time")


def load_manifest(path=None) -> dict:
    with open(path or MANIFEST, encoding="utf-8") as f:
        return json.load(f)


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(CHUNK), b""):
            h.update(b)
    return h.hexdigest()


def _check_entry(e: dict, allow_http: bool):
    for k in ("name", "version", "url", "size", "sha256"):
        if e.get(k) in (None, ""):
            raise FetchError(f"{e.get('name', '?')}: '{k}' is not set in the manifest, so it is not pinned yet")
    if len(e["sha256"]) != 64 or any(c not in "0123456789abcdef" for c in e["sha256"]):
        raise FetchError(f"{e['name']}: the SHA-256 in the manifest is malformed")
    if not e["url"].startswith("https://") and not (allow_http and e["url"].startswith("http://127.0.0.1")):
        raise FetchError(f"{e['name']}: only https:// downloads are allowed")


TARS = (".tar.xz", ".tar.gz")


def suffix(url: str) -> str:
    """The archive's suffix from its URL: .tar.xz and .tar.gz whole, else the last one (.zip)."""
    p = PurePosixPath(url)
    two = "".join(p.suffixes[-2:]).lower()
    return two if two in TARS else (p.suffix or ".bin")


def archive_path(e: dict, cache: Path) -> Path:
    return Path(cache) / f"{e['name']}-{e['version']}{suffix(e['url'])}"


def fetch(e: dict, cache, progress=None, allow_http=False, timeout=60, note=None) -> Path:
    """Download entry `e` into `cache` (or reuse a matching copy) and return its path.
    progress(done, total) is called as bytes arrive. note(line): a line for setup's log, when the download goes
    through Windows' own curl."""
    _check_entry(e, allow_http)
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    final = archive_path(e, cache)
    if final.is_file():
        if final.stat().st_size == e["size"] and _sha256(final) == e["sha256"]:
            return final
        final.unlink()                      # a stale or damaged copy
    part = final.with_name(final.name + ".part")
    done = part.stat().st_size if part.is_file() else 0
    if done > e["size"]:
        part.unlink()
        done = 0
    if done < e["size"]:
        req = urllib.request.Request(e["url"], headers={"User-Agent": "des-setup"})
        if done:
            req.add_header("Range", f"bytes={done}-")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                if done and r.status != 206:   # the server ignored the Range: start over
                    done = 0
                with open(part, "ab" if done else "wb") as f:
                    while True:
                        b = r.read(CHUNK)
                        if not b:
                            break
                        f.write(b)
                        done += len(b)
                        if done > e["size"]:
                            raise FetchError(f"{e['name']}: the download is larger than the manifest says")
                        if progress:
                            progress(done, e["size"])
        except OSError as err:
            curl = windows_curl() if cert_failed(err) else None
            if not curl:
                raise FetchError(f"{e['name']}: the download stopped ({err}); it resumes next time") from err
            if note:
                note(f"note {e['name']}: Python's check did not accept the server's certificate "
                     f"({getattr(err, 'reason', err)}); downloading through Windows' own curl, which checks it as "
                     "Windows does")
            _curl_fetch(curl, e, part, progress, timeout, allow_http)
    if not part.is_file() or part.stat().st_size != e["size"]:
        raise FetchError(f"{e['name']}: the download is incomplete; it resumes next time")
    got = _sha256(part)
    if got != e["sha256"]:
        part.unlink()
        raise FetchError(f"{e['name']}: the download does not match its pinned SHA-256 (got {got}); deleted")
    os.replace(part, final)
    return final


def _bad_name(name: str) -> bool:
    name = name.replace("\\", "/")
    parts = PurePosixPath(name).parts
    return name.startswith("/") or (parts and ":" in parts[0]) or ".." in parts


def _link_leaves(m: tarfile.TarInfo) -> bool:
    """A link whose target is not inside the archive's folder: an absolute one, or one that '..' takes out. A
    symlink's target is from the link's own folder, a hard link's from the top."""
    target = m.linkname.replace("\\", "/")
    if target.startswith("/") or (target[1:2] == ":"):
        return True
    base = posixpath.dirname(m.name) if m.issym() else ""
    return posixpath.normpath(posixpath.join(base, target)).split("/")[0] == ".."


def _unpack_tar(archive: Path, tmp: Path):
    """A .tar.xz or .tar.gz, by tarfile's "data" filter (Python 3.12 and later): it refuses absolute paths, paths
    that leave the folder and device files, and keeps the owner's execute bit, which a zip cannot carry (the
    compiler must run). A link is kept only if it stays inside the folder (Linux's Python has 1,048 of them, such
    as bin/python3 -> python3.13); where links cannot be made, tarfile puts a copy of the target there."""
    if not hasattr(tarfile, "data_filter"):
        raise FetchError(f"{archive.name}: unpacking it needs Python 3.12 or later")
    with tarfile.open(archive, "r:*") as t:
        for m in t.getmembers():
            if _bad_name(m.name):
                raise FetchError(f"{archive.name}: refusing the entry {m.name!r}")
            if (m.issym() or m.islnk()) and _link_leaves(m):
                raise FetchError(f"{archive.name}: refusing the link {m.name!r} -> {m.linkname!r}")
            if not (m.isfile() or m.isdir() or m.issym() or m.islnk()):
                raise FetchError(f"{archive.name}: refusing the special file {m.name!r}")
        try:
            t.extractall(tmp, filter="data")
        except tarfile.FilterError as err:
            raise FetchError(f"{archive.name}: refusing an entry ({err})") from err


def unpack(archive: Path, dest) -> Path:
    """Unpack a .zip, .tar.xz or .tar.gz into dest (replacing what is there) and
    return dest. Entries with absolute paths, drive letters or '..' are refused,
    and so are a zip's links and a tar's links that leave the folder."""
    dest = Path(dest)
    tmp = dest.with_name(dest.name + ".tmp")
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    if Path(archive).name.lower().endswith(TARS):
        try:
            _unpack_tar(Path(archive), tmp)
        except (FetchError, OSError, tarfile.TarError) as err:
            shutil.rmtree(tmp, ignore_errors=True)
            if isinstance(err, FetchError):
                raise
            raise FetchError(f"{Path(archive).name}: it cannot be unpacked ({err})") from err
        if dest.exists():
            shutil.rmtree(dest)
        os.replace(tmp, dest)
        return dest
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            if _bad_name(info.filename):
                shutil.rmtree(tmp)
                raise FetchError(f"{archive.name}: refusing the entry {info.filename!r}")
            # Unix symlinks have S_IFLNK in the high attribute bits.
            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                shutil.rmtree(tmp)
                raise FetchError(f"{archive.name}: refusing the symlink {info.filename!r}")
        z.extractall(tmp)
    if dest.exists():
        shutil.rmtree(dest)
    os.replace(tmp, dest)
    return dest


def unpacked(e: dict, tools) -> bool:
    """The tool is unpacked in tools/<name>-<version> from the archive the entry pins
    (its stamp file says so). The host checks the same stamp for Python (docs/88)."""
    stamp = Path(tools) / f"{e['name']}-{e['version']}" / ".des-setup-sha256"
    return stamp.is_file() and stamp.read_text().strip() == e.get("sha256")


def ensure(e: dict, cache, tools, progress=None, allow_http=False, note=None) -> Path:
    """The tool unpacked in tools/<name>-<version>, fetched first if needed.
    A stamp file records the SHA-256 it was unpacked from. note: as fetch's."""
    dest = Path(tools) / f"{e['name']}-{e['version']}"
    stamp = dest / ".des-setup-sha256"
    if unpacked(e, tools):
        return dest
    a = fetch(e, cache, progress, allow_http, note=note)
    if not a.name.lower().endswith((".zip",) + TARS):
        raise FetchError(f"{e['name']}: only .zip, .tar.xz and .tar.gz archives can be unpacked")
    unpack(a, dest)
    stamp.write_text(e["sha256"] + "\n")
    return dest
