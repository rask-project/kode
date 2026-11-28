# Kode — Remote folders over SSH

A plan for opening a folder on another machine over SSH and working on it as
if it were local: the sidebar, documents, saving, Find File and the language
servers, without the editor waiting on the network while one types.

Status: **proposal**, not yet a stage of `ROADMAP.md`.

---

## 1. The problem

What makes remote editing feel slow is never typing: Kode already edits a
`TextDocument` in memory, and the disk is touched only to open, save and
watch. The latency hurts where the editor talks to the file system:

| Operation | Local today | Over the network |
|---|---|---|
| Typing, undo, highlighting, layout | memory | memory — unchanged |
| Opening, saving a file | `QFile`, `QSaveFile` | round trips + transfer |
| Sidebar (`FileTree`) | `QFileSystemModel` | a round trip per folder |
| Changes on disk | `QFileSystemWatcher` | must be watched on the host |
| Find File (`FileSearch`) | `QDirIterator` | thousands of entries |
| Language servers | `QProcess` | must run on the host (section 3) |

The "local buffer" already exists: the open document *is* it. What is
missing is a remote back end for the operations above that costs one round
trip per intention, never blocks the GUI thread, and pushes changes instead
of being asked for them.

---

## 2. Choice: an agent on the host

How other editors do it:

| Editor | Model |
|---|---|
| VS Code (Remote - SSH) | An agent (`vscode-server`) on the host: files, search, terminals, extensions, language servers; buffers local |
| Zed (remote development) | An agent (`zed-remote-server`) over the SSH connection's stdio (protobuf); worktree, LSP and search remote |
| JetBrains | Gateway: the IDE's backend on the host · Deployment: a local mirror uploaded over SFTP |
| Emacs TRAMP | No agent: shell commands over a multiplexed SSH connection; processes on the host |
| Sublime (SFTP), BBEdit, Nova | SFTP open/save; no remote language servers |

Kode follows VS Code and Zed: a small binary of its own, `kode-remote`,
uploaded to the host and spoken to over SSH. Compared with the alternatives:

| Operation | Mirror (rsync/Mutagen) | SFTP + commands | **Agent** |
|---|---|---|---|
| Start | copy the whole tree | immediate | immediate |
| Open a file | 0 RTT | ~2 RTT | **1 RTT** |
| Save | local, uploaded later | ~3 RTT, a race window | **1 RTT, checked on the host** |
| A change on the host | watchers on both sides | polling | **pushed** (inotify/FSEvents) |
| Listing folders | local | ~3 RTT per folder | **1 RTT for all** |
| Find File | local | one command, cached | **listed on the host, kept current by the watcher** |
| Conflicts | anywhere in the tree | at save | **at save, without a race** |
| Builds and tasks later | separate | separate | **the same process service** |

Rejected: mounting (SSHFS + macFUSE: every `stat` crosses the network,
needs a kernel extension) and mirroring (two sources of truth, every
`git checkout` or build a sync conflict; Mutagen itself uploads an agent).

Hosts where the agent cannot run (an architecture it is not built for, a
`noexec` home) are not supported at first; the file-system seam
(section 8) leaves room for an SFTP back end later if they matter.

---

## 3. Language servers run on the host

*Could the server run on the Mac with the agent providing it a file
system?* Not usefully:

- **Servers read the disk themselves**, not through LSP. clangd opens every
  header an `#include` reaches, `compile_commands.json`, and runs the
  compiler named there (`--query-driver`) to learn its system includes;
  rust-analyzer loads the crates and runs `cargo metadata`; pyright reads
  the virtualenv's `site-packages`; gopls runs `go list`. LSP gives them
  no way to ask the client for files (only for the open documents' text),
  so the only way to give them a remote file system is mounting it —
  rejected above.
- **The toolchain is the host's.** The paths in `compile_commands.json`, the
  Linux system headers, the cross compiler, the project's Python, the
  `cargo` registry live there. On the Mac they do not exist, or are the
  wrong ones (macOS's libc++, not the host's libstdc++): wrong diagnostics
  and completions.
- **The traffic goes the wrong way.** Indexing a C++ project reads
  thousands of headers — gigabytes over the network, one round trip each.
  Running on the host, the server reads its local disk and only LSP's
  messages cross: small, and one round trip per request (a completion
  costs its RTT plus the server's time).

So the agent starts the servers on the host (its process service), and
Kode talks LSP to them through it, exactly as it talks to a local one over
stdio. The only translation is the URIs (section 9, A5).

---

## 4. The agent

### Shape

```
Mac                                         Host
Kode ── ssh (ControlMaster) ── kode-remote bridge ── unix socket ── kode-remote daemon
                                (stdio ↔ socket)                     ├── session (per window set)
                                                                     │   ├── fs
                                                                     │   ├── watch
                                                                     │   ├── search
                                                                     │   └── process ── clangd, …
                                                                     └── idle exit
```

- **Daemon + bridge**, as VS Code's server: `kode-remote daemon` runs once per
  user and version, listening on a unix socket (in `$XDG_RUNTIME_DIR`,
  only the user's; never TCP — section 7). SSH starts `kode-remote bridge`, which only copies
  bytes between its stdio and the socket (starting the daemon if absent).
  A dropped connection kills the bridge, not the daemon: language servers —
  and, later, builds — keep running, and Kode reattaches (A6).
- **Sessions**: each Kode connection gets a session id; its watches,
  channels and processes belong to it. A session without a client is kept
  for a while (10 minutes), then closed; the daemon exits after its last
  session.
- **Services**: every feature is a service with its own methods
  (`fs/…`, `watch/…`, `search/…`, `process/…`). The transport, sessions and
  channels know nothing about them. A new feature — tasks and builds,
  a terminal, port forwarding, text search, whatever comes — is one new
  service registered in the daemon and one client class in Kode; nothing
  else changes.

### Code and build

- **C++23, no Qt**: the standard library and POSIX, plus
  [yyjson](https://github.com/ibireme/yyjson) (C, MIT, no exceptions) for
  JSON. A static Qt for Linux is a heavy build for what the agent needs.
  The repository's C++ rules apply (no exceptions, `std::expected`, single
  ownership, no recursion); the Qt-type rules cannot, so the agent and the
  shared protocol code use `std::int32_t`, `std::vector` and the like —
  `AGENTS.md` gains that exception for `remote/`.
- **Platforms**: Linux x86_64 and arm64 (static, musl), macOS arm64. Built
  by `tools/build-remote.sh` in an Alpine container (Docker/Podman) locally
  and in CI (`ubuntu-24.04` and `ubuntu-24.04-arm` runners); the macOS
  build is part of the normal CMake build (tests run it).
- **Bundled** in `Kode.app/Contents/Resources/remote/<os>-<arch>/kode-remote`
  with its SHA-256; the agent's version is Kode's (exact match: one
  installation per version, no compatibility matrix).
- **Event loop**: single-threaded (`epoll` on Linux, `kqueue` on macOS) for
  the socket, channels, processes and timers; a small worker pool for
  blocking work (reading big files, walking trees, hashing), posting results
  back through a pipe. No locks on the loop's state.

### Layout in the repository

```
remote/
  CMakeLists.txt         kode-remote; static on Linux
  protocol/              shared with Kode, std-only, header + .cpp
    Frame.h/.cpp         frame types, FrameReader (pieces in, frames out)
    Methods.h            method names, error codes, capability names
  agent/
    main.cpp             daemon | bridge | --version
    EventLoop, WorkerPool
    Daemon, Session, Channel
    Service.h            name, capabilities, handle(method, params, Reply&)
    services/            Fs, Watch, Search, Process   (later Task, Terminal…)
src/fs/                  FileSystem, LocalFileSystem, Process, Hosts
src/remote/              Connection, Bootstrap, ChannelDevice,
                         RemoteFileSystem, RemoteProcess
```

---

## 5. Protocol

### Transport

OpenSSH (`/usr/bin/ssh` via `QProcess`), not an embedded library: the
user's `~/.ssh/config` (Host, User, Port, IdentityFile, ProxyJump), the
agent (1Password, Secretive), `known_hosts` and FIDO keys work as they do in
the terminal.

```
ssh -o ControlMaster=auto -o ControlPath=~/Library/Caches/Kode/ssh/%C \
    -o ControlPersist=10m -o ServerAliveInterval=15 -o ServerAliveCountMax=3 \
    host -- ~/.cache/kode/remote/<version>/kode-remote bridge
```

Passwords, passphrases, 2FA codes and unknown host keys go through
`SSH_ASKPASS` (a helper in `Kode.app` asking the running app for a native
prompt; `SSH_ASKPASS_REQUIRE=force`). Nothing is stored.

### Frames

One byte stream, many channels, binary data never in JSON:

```
frame   = length:u32be  type:u8  channel:u32be  payload[length - 5]

type    0  MESSAGE   JSON-RPC 2.0 (channel 0 only)
        1  DATA      bytes of an open channel
        2  EOF       this side sends no more on the channel
        3  CREDIT    payload u32be: bytes the sender of this frame accepts more
        4  RESET     the channel is gone (abandoned, or its owner closed)
```

- **Channel 0** carries JSON-RPC: requests, responses and notifications,
  methods named `service/method`; `$/cancel` cancels a request.
- **Data channels** are opened by a request (`fs/read`, `process/spawn`…)
  whose result names them; client-opened ids are odd, agent-opened even, so
  they never collide. A file's bytes, a process's stdin/stdout/stderr, a
  long listing stream on one.
- **Flow control** per channel by credit (as SSH and HTTP/2): 1 MB to start,
  frames at most 256 KB, so a server spewing output or a large file never
  delays channel 0. Channel 0 is not flow-controlled and is sent first.
- The framing lives in `remote/protocol`, compiled into both sides and
  tested once (pieces of any size, as `MessageReader` is).

### Handshake

```
→ initialize { protocol: 1, client: "1.3.0", session?: "…" }
← { agent: "1.3.0", session: "…", resumed: false,
    platform: { os: "linux", arch: "x86_64", home: "/home/u", shell: "/bin/zsh" },
    capabilities: { fs: ["trash", "edits"], watch: ["inotify"],
                    search: ["gitignore"], process: [] } }
```

Capabilities describe what this host offers, not versions: `watch` may be
`["polling"]` where inotify is unavailable, `process` gains `"pty"` the
day terminals and builds need it. Kode enables features by them; a method
the agent lacks answers `MethodNotFound`.

### Services

**fs** — paths are the host's, absolute, canonical (`realpath`), compared
byte for byte.

| Method | Does |
|---|---|
| `fs/stat {paths[]}` | several at once: type, size, mtime (ns, the host's), mode, link target |
| `fs/list {folders[]}` | several folders at once (every expanded one in one request) |
| `fs/read {path}` | `{info, hash}` and a channel with the bytes (small files inline) |
| `fs/write {path, expect?, mode?}` + channel | atomic save (section 6); returns the new `info` |
| `fs/rename`, `fs/copy`, `fs/remove`, `fs/makeFolder`, `fs/trash` | as `kode::files` (a taken name gets " 2"); `trash`: the freedesktop Trash on Linux, `~/.Trash` on macOS |

**watch**

| Method | Does |
|---|---|
| `watch/start {root, exclude[]}` | a watch id; inotify (recursive, coalesced 50 ms) or FSEvents |
| notification `watch/changed {id, seq, changes[]}` | created / modified / removed / renamed, with each path's new `info` |
| `watch/since {id, seq}` | after a reconnect: the changes missed, or `resync` when the log overflowed |

inotify's per-user limit (`max_user_watches`) is reported; past it the watch
falls back to polling the expanded folders and open documents.

**search**

| Method | Does |
|---|---|
| `search/files {root, showHidden}` | every file on a channel (`.git`, build, `node_modules`… out, `.gitignore` honored), then updates as the watch sees files come and go |

Scoring stays in Kode (`FileSearch::score()`): it runs on every keystroke and
must not wait for the network. Text search across files is a later method of
this service.

**process** — the seam for everything that runs on the host.

| Method | Does |
|---|---|
| `process/spawn {argv, cwd, env?, stdio}` | a process id and its channels; `argv[0]` found on the login shell's PATH (read once per daemon: `$SHELL -ilc 'env -0'`) |
| `process/signal {id, signal}` | TERM, KILL, INT |
| notification `process/exited {id, code \| signal}` | |
| `process/list` | the session's processes (for reattaching) |
| `process/which {names[]}` | which programs exist (Settings' language servers found) |

Language servers are processes with stdin/stdout channels. A build later is
the same call — plus `pty`, an output kept in a bounded ring buffer for
reattaching, and a `task` service on top for problem matching — without
touching the transport.

---

## 6. Consistency rules

The file on the host is the only source of truth; a document is an edited
view of one version of it.

1. **A document remembers the version it read**: size, mtime and the
   SHA-256 of the bytes (computed by the agent while reading).
2. **Saving is conditional and checked on the host**: `fs/write` carries
   `expect: {size, mtime, hash}`; the agent writes `.name.kode-XXXX` in the
   same folder, `fsync`s, checks the target is still that version
   (`fstat`, and the hash when size and mtime are ambiguous) and renames over
   it — all inside one request, so nothing on the Mac can race it. A
   mismatch fails with `Conflict` and the new `info`, which shows today's
   "changed on disk" alert (keep mine / reload). Never overwrite silently.
3. **The file keeps what it was**: permissions and owner (when allowed),
   written through a symbolic link to its target; a folder not writable
   falls back to writing in place, with the same check.
4. **Own saves are not changes**: the write returns the new `info`; the
   watch's event for it matches and is dropped (as `Document` tells its own
   saves apart now).
5. **Times are the host's**, compared only with the host's (clock skew).
6. **Bytes, not text, cross the wire**: decoding, encoding, BOM and line
   endings stay in `TextCodec`, so a remote save writes what a local one
   would.
7. **Losing the connection loses no work**: documents stay editable (the
   text is local); the host shows *Offline* in the sidebar's footer, saving
   is refused with a clear message (the document stays modified); Kode
   reconnects with backoff (1, 2, 4… 30 s, at once on window activation or
   a network change) and resumes the session: `watch/since` brings the
   changes missed (or a resync: `fs/stat` of every open document, folders
   listed again), then rule 2 decides each pending save.
8. **One connection per host**, owned by `Hosts`, shared by every window,
   so no two windows disagree about a file.
9. **Large files** stream through a channel into `readFile()` off the GUI
   thread, as from 2 MB locally; the LSP's 8 MB limit stays.
10. **Quitting** with unsaved remote documents goes through `QuitGuard`;
    offline, "Save" is not offered for them.

Out of scope: editing as another user (`sudo`), two people editing one file
(collaboration), a local mirror.

---

## 7. Security

The agent adds no new way into either machine: it listens on no network
port, runs as the user who logged in, and does only what that user could
already do in an `ssh` shell. What it must guarantee is that nothing beyond
that leaks — in either direction.

### Who is trusted

| Party | Trusted for |
|---|---|
| The Mac and the user running Kode | everything |
| SSH (OpenSSH, the user's config and keys) | encryption, authenticating the host (`known_hosts`) and the user |
| The host's account (and its root) | its own files and processes — **not** the Mac: whatever the agent or a server running there sends is untrusted input |
| Other users on the host | nothing |

A compromised host account can read and change the remote project — as it
could without Kode. It must not be able to reach anything on the Mac.

### The link: SSH and nothing else

- Every byte goes through the user's SSH connection: encrypted, the host's
  key checked against `known_hosts` (`StrictHostKeyChecking` as the user set
  it; an unknown key asked through askpass, never accepted silently).
- Kode's connections add `-o ForwardAgent=no -o ForwardX11=no
  -o ClearAllForwardings=yes -o PermitLocalCommand=no` over the user's
  config: a host that is compromised while Kode is connected must not use
  the Mac's keys (agent forwarding) or open ports back to it. Port
  forwarding, when it comes, opens exactly the ports asked for.
- The control socket (`~/Library/Caches/Kode/ssh/`, folder `0700`) is the
  user's only.
- **Credentials never pass through Kode** except as typed into the askpass
  prompt, handed to `ssh` and forgotten: no passwords stored, no keys read.
  The askpass helper reaches the app through a unix socket in the app's
  caches folder (`0600`) and a random token in its environment, set by Kode
  for the `ssh` it started, so no other process can make Kode show a
  prompt.

### The daemon on the host

- **No network listener**: a unix socket only, in `$XDG_RUNTIME_DIR`
  (`/run/user/<uid>`, a `0700` tmpfs) when there is one, else
  `/tmp/kode-<uid>/` — created with `0700`, checked at every start to be a
  real folder (`lstat`, not a link) owned by the user with no access for
  others, else refused. Not in `$HOME`: on NFS homes sockets fail and the
  folder is shared between machines.
- **Peer check**: every connection's uid is read from the kernel
  (`SO_PEERCRED` on Linux, `getpeereid()` on macOS) and must be the
  daemon's own; anything else is closed before a byte is read.
- **Sessions are random 128-bit ids**: resuming one (A6) needs it, so two
  Macs of the same user never attach to each other's processes.
- **The binary**: installed in a `0700` folder, its SHA-256 checked against
  the one bundled in `Kode.app` before every start (an upload cut short or
  a file changed is caught and replaced). It cannot protect itself from
  the account it runs as — nothing can — and does not try.
- **No privilege**: never setuid, never `sudo`, nothing in its methods
  that the user could not do in a shell.
- **Its files**: temporary saves created with `O_EXCL | O_NOFOLLOW` and the
  target's mode (never readable by others when the target is not), removed
  on failure; the log `0600`, holding no file contents, no environment, no
  arguments of processes.

### The Mac, facing an untrusted host

The important half. Everything from the agent — frames, JSON, paths, file
names, a language server's messages — is input from another machine.

- **Parsing is bounded**: a frame's length is checked against the limit
  (256 KB of payload, a JSON message at most 16 MB) before anything is
  allocated; malformed frames or JSON close the connection. The frame
  reader and the message decoding are fuzzed (libFuzzer, in CI).
- **Resources are bounded**: channels by credit (a host cannot push more
  than Kode accepted), listings and search results capped (as
  `FileSearch`'s 200 shown), notifications coalesced, a document's size
  checked before it is read (as the 8 MB LSP limit); pending requests time
  out.
- **Remote paths stay remote**: a path from the host only ever becomes an
  `ssh://host/…` URL of *that* host. No method lets the host name a file on
  the Mac, open a URL, or start anything locally. The URI translation for
  language servers maps every `file://` to the host's `ssh://` — never to a
  local file — so a server's `workspace/applyEdit` or a definition can only
  touch the host's files; anything else is dropped. `window/showDocument`
  and similar requests are answered empty, as now.
- **Nothing from the host runs on the Mac**: a remote binary file is not
  handed to a local app; if it ever is (the open decision), it is copied
  with the `com.apple.quarantine` attribute so Gatekeeper treats it as a
  download.
- **Names are shown, not interpreted**: control characters and
  bidirectional overrides in remote file names are shown escaped in the
  sidebar, Find File and alerts.
- **The agent never sees what it does not need**: the Mac's paths, its
  environment and Kode's settings other than the host's own are never sent.

---

## 8. Kode's side

### The file-system seam

Every place that touches the file system goes through one interface, with
the local implementation (today's code, moved) and the remote one:

```
kode::fs::FileSystem              (abstract)
  stat(paths) list(folders) read(path) write(path, bytes, expect)
  rename copy remove makeFolder trash realPath
  watch(root) -> changes            start(argv, cwd) -> Process
kode::fs::Process                 (a QIODevice: stdin/stdout, finished(code))
kode::fs::Hosts                   (QML singleton)
  forUrl(url) -> FileSystem&       local for file://, the host's for ssh://
  state(host)                      Connecting / Installing / Online / Offline / Failed(message)
```

- Asynchronous everywhere (`QFuture<std::expected<T, FsError>>`); the local
  implementation runs the same calls on `QThreadPool`, as large files load
  today.
- `Hosts` owns each `remote::Connection` (`std::unique_ptr`, created on first
  use, closed after its last window); documents, trees and servers keep
  the URL and look it up — no shared ownership.
- `FsError`: not found, permission, conflict (with the new info),
  disconnected, other(message).

### Addresses

`ssh://[user@]host[:port]/absolute/path` in `Document`, `Session`, Open
Recent and the `kode` command (`kode ssh://box/srv/app`); `host` as named in
`~/.ssh/config`, resolved by ssh.

### What moves onto the seam

| Today | Remote |
|---|---|
| `Document`: `QFile`, `QSaveFile`, `QFileSystemWatcher`, `startFile()` | `read()`/`write(expect)`; changes from the folder's watch |
| `TextCodec::readFile(QIODevice&)` | unchanged: a channel is a `QIODevice` (`ChannelDevice`) |
| `FileTree` over `QFileSystemModel` | `RemoteFileModel` (`QAbstractItemModel`, the same roles) fed by `fs/list` and the watch; `FileTree`'s sorting and filtering stay as the proxy over either |
| `FileOperations` | the `fs/*` methods |
| `FileSearch` (`QDirIterator`) | `search/files`, scored locally as now |
| `FileIcons` by path | by extension through `UTType` |
| `OpenDocuments`'s binary check | the first 8000 bytes and the extension |
| `LanguageServer`'s `QProcess` | an `fs::Process` (local: a `QProcess`; remote: `process/spawn`) |
| `LanguageServers`: shell PATH, `.git` root, build folders | `process/which`, `fs/stat` on the host |
| `Session` (canonical folder URL) | works with `ssh://` URLs |
| `RecentDocuments` (`NSDocumentController`, local only) | remote folders in Kode's own list, beside the local ones |

---

## 9. Stages

Each ends with something usable and tested; none depends on a later one.
Status: ✅ done · 🚧 in progress · ⬜ not started.

| Stage | Status |
|---|---|
| A0 — The seam | folded into A3–A5 |
| A1 — Protocol and agent skeleton | ✅ |
| A2 — Bootstrap and connection | ✅ |
| A3 — Remote documents | ✅ |
| A4 — Remote folders | ⬜ |
| A5 — Remote language servers | ⬜ |
| A6 — Resilience and measurements | ⬜ |

### A0 — The seam (folded into A3–A5)

First planned as its own stage: `kode::fs::FileSystem`, `Process` and
`Hosts` with only the local implementation, everything moved onto them
first. Dropped as a stage: a refactor of working local code with nothing
yet to show for it, and an interface designed before the remote side it
serves. Each stage brings the part of the seam it needs, with its remote
implementation beside the local one — documents in A3, the tree, its
operations and Find File in A4, processes in A5 — and the local code is
moved onto it then, its tests still passing.

### A1 — Protocol and agent skeleton ✅

Done: `remote/protocol` (`Frame`, `FrameReader`, `Methods.h`: frames
bounded by type before anything is allocated), and `kode-remote`
(`remote/agent`: C++23, POSIX and yyjson, `-fno-exceptions`) — a
`poll(2)` event loop, sessions, the service registry (`Service`, `Reply`:
answered now or later, an unanswered one answers with an internal
error), the handshake (protocol version, a random 128-bit session id,
platform, capabilities) and `system/ping`; `serve` (one session on
stdio), `daemon` (the runtime folder checked, one per user and version by
`flock`, a unix socket `0600`, each peer's uid checked, idle exit) and
`bridge` (checks the folder and the daemon's uid, starts the daemon when
none answers, copies stdio). Kode's `kode::remote::Connection` (the
process, requests by id with handlers, notifications, stderr kept for
ssh's messages, everything from the agent checked: a broken frame or
message ends it). `tst_remote`: frames in random pieces, oversized and
unknown frames refused, the handshake, requests before initialize
refused, a broken frame ending the session, pending requests failed on
close, two connections through the bridge to one daemon (separate
sessions; idle exit), an open runtime folder refused.

Moved to later stages, where something uses them: the worker pool (A3,
reading files), data channels and their credit (A3, `fs/read`), fuzzing
the reader (A2, with the CI job). Not testable here: a peer of another
uid (needs a second user).

### A2 — Bootstrap and connection ✅

Done: `kode::remote::ssh` (`src/remote/Ssh`: the destination checked — no
option, space or control character —, ssh's arguments with
`ControlMaster` in `~/.ssh/kode` (short: a control socket's path must fit
in 104 bytes), `ServerAlive`, the forwardings and `PermitLocalCommand` off;
the probe, install and bridge commands for `sh -c`, whatever the login
shell; `~/.ssh/config`'s hosts, its Includes read in place); `Host` (the
probe — system, architecture, home, the installed agent's SHA-256 —, the
agent installed when missing or changed: folders `0700`, other versions
removed, its hash checked after; the bridge; states Disconnected,
Connecting, Installing, Online, Offline, Failed; a loss once online retried
with backoff, 1 to 30 s, `reconnectNow()`; authentication, host keys and
unknown names fail without retrying); `Hosts` (QML singleton: one `Host`
per destination; `kode-askpass` — `remote/askpass` — asks it through a
socket in `$TMPDIR` with a random token, compared in constant time; one
prompt at a time: a secure field for passwords, passphrases and codes, a
question for an unknown host key). Rask.Dialogs gained `TextPrompt` (an
`NSAlert` with a text field, secure on request). File > Connect to
Server… (⇧⌘K): a host from `~/.ssh/config` or typed, its status, Connect
and Disconnect. `Kode.app` bundles `kode-askpass` (`Contents/MacOS`) and
the agents (`Contents/Resources/remote/<os>-<arch>`: this Mac's, and
`KODE_REMOTE_AGENTS_DIR`'s). CI (`remote-agents.yml`, called by CI and by
the release): the Linux agents in Alpine containers (static, x86_64 and
arm64, each checked by `tools/remote-smoke.py`'s handshake) and two
minutes of fuzzing the frame reader (libFuzzer, ASan, UBSan); the release
bundles them. `remote/` also builds as a project of its own (no Qt).
Tests (`tst_remote`), with a stand-in ssh running the commands here: the
agent installed (`0700`) and then found by its hash, a changed one
installed again, no agent for the platform, a permanent error not retried,
a lost daemon (offline, retried, online again with a new session); the
askpass answered, cancelled, a wrong token refused, the prompt kinds; the
destinations, quoting, the probe's parsing, the config's hosts.

Not done here: a test against a real `sshd` (none on this Mac; the stand-in
runs the same commands). A dev build reinstalls a changed agent of the
same version, but a daemon of that version already running keeps serving
until it goes idle (releases have a version each).

### A3 — Remote documents ✅

Done. The agent: channels with credit (`Channel.h`: a `Source` the agent
reads as the client's credit allows, a `Sink` for what the client sends;
CREDIT granted back as each piece is written; a session's output kept
under 1 MB so channel 0 is never held behind data), SHA-256 of its own
(`Sha256`), and the `fs` service: `fs/stat` (several paths; links with
their target and its type), `fs/list` (at most 50 000 entries), `fs/read`
(the bytes on a channel of the agent's, reset when the file changed while
read) and `fs/write` (the bytes on the client's channel: a temporary file
beside the target, `fsync`, the version checked — same size and time, or
same size and SHA-256 —, mode and owner kept, renamed over it; through a
link to what it names; in place where the folder takes no new file;
folders made on request; a conflict answers `error::conflict` with the
version now). Kode: `Connection`'s channels (`receive()`, `openChannel()`,
`send()`), `remote::files` (`read`, `write`, `stat`, `list`; the version
and the SHA-256 of the bytes read or written), `Hosts::shared()` (the QML
singleton's instance, for C++), `Host::whenOnline()` (connecting first;
waiting through retries), `ssh::destinationOf()`/`fileUrl()` (an
`ssh://[user@]host[:port]/path` URL and its destination). `Document`: an
ssh:// file read through the agent and decoded off the GUI thread (a NUL
in its first 8000 bytes: not text), saved through it with the version
read as `expect`; every save now ends with `saved()` or `saveFailed()`
(at once for a local file), so `EditorWindow.qml`'s save and review wait
for it; a save on its way keeps later edits modified
(`TextDocument::beginSave()`/`finishSave()`); a conflict emits
`changedOnDisk()`, whose Keep Mine (`keepMine()`) makes the next save
replace the host's version; the host asked every 5 s whether the file
changed (reloaded when unmodified); a file not there yet opens empty and is
written with its folders; saving while the host is not connected refuses,
the changes kept. Connect to Server… lists the host's files once online
(`RemoteFolder`, a model over `fs/list`: folders first, hidden ones left
out, from the home); a double click enters a folder or opens a file in the
window. `kode ssh://host/path` opens it. Tests (`tst_remote`, over
`serve` and through the stand-in ssh): files written and read back (hashes
alike, no temporary left), 5 MB both ways (past the first credit), a
conflicting write refused with its version, a touched file with the same
bytes written, the mode kept and a link written through, a read-only
folder written in place, a folder listed, a missing file; a remote
`Document` opened and saved, its encoding and CRLF kept, a conflict and
Keep Mine, an edit during a save kept modified, a change on the host
reloaded, a missing file started and saved.

Moved to A4: Open Recent for remote files (`RecentDocuments` keeps local
ones only) and the sidebar's host state. Not done: a test of a connection
dropped mid-save (the save fails with `saveFailed()`, the document stays
modified — the path is the same as any error's).

### A4 — Remote folders

The `watch` and `search` services; `RemoteFileModel` under `FileTree`
(listed on expand, kept current by the watch), the file operations and
Trash, Find File over `search/files`, icons by type, sessions per remote
folder, the host's state in the sidebar's footer. Tests: a folder changed
on disk while watched (create, rename, remove, a burst coalesced), the
inotify limit's fallback (in the Linux CI job).

### A5 — Remote language servers

The `process` service; `LanguageServer` over `fs::Process`; URIs
translated at its boundary both ways (the host's `file:///srv/app/x.cpp` ↔
Kode's `ssh://box/srv/app/x.cpp`, in every message: documents, locations,
`workspace/applyEdit`, related information); servers found with
`process/which` on the host's PATH, roots and build folders with `fs/stat`;
Settings > Language Servers per host. Tests: the URI translation; with
clangd installed, a real round trip through the agent over pipes.

### A6 — Resilience and measurements

Sessions resumed after a dropped connection (`initialize {session}`,
`watch/since`, `process/list`: the language servers kept, their unanswered
requests failed and re-asked), idle exit, the daemon's log
(`~/.cache/kode/remote/<version>/kode.log`, rotated). Measured on a slow
link (`dnctl`/`pfctl` adding 150 ms): opening a large repository's tree,
Find File on 100 000 files, the delay of an outside change, saving a 10 MB
file. Then, only if the numbers ask for them: compression of large
transfers (zstd per channel), and saves as edits (`fs/write` with the
changed byte ranges against `expect.hash`, the agent checking the result's
hash; the `"edits"` capability).

### Later (not planned now)

On the same services, without changing the transport: builds and tasks
(`process` with `pty`, output kept for reattaching, a `task` service with
problem matching into the diagnostics), a terminal (or handing
`ssh -t host 'cd … && exec $SHELL'` to Konsoll), text search across files,
port forwarding.

---

## 10. Open decisions

- A remote binary file: refused, or copied to a temporary folder and handed
  to the system's app.
- Whether an SFTP back end is ever needed for hosts the agent cannot run on.
