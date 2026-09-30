---
name: openwrt-feature-state-verification
description: Use when verifying OpenWrt feature support today.
---

# Verifying "what does OpenWrt support today" (packages / kmods / protos)

Never answer from memory: OpenWrt moves fast (new releases, apk switch, feed moves). Every claim needs a fetchable URL + date.

## 1. What is actually shipped (binary truth)
- Releases list + which is stable: `curl -s https://downloads.openwrt.org/releases/ | grep -oE 'href="2[0-9]\.[0-9]+'` (symlinks `latest`, `latest-<major>` point at stable).
- Feed packages are **apk** since 25.12 (no `Packages`/`Packages.gz`, no `Packages.manifest`):
  `https://downloads.openwrt.org/releases/<rel>/packages/<arch>/<feed>/` -> grep hrefs for names.
- `index.json` next to `packages.adb` is only name->version (**no sizes**). For real metadata
  (installed-size, file-size, depends, provides, paths) decode `packages.adb` with Alpine's static
  apk-tools 3: `curl -o apk.static.apk https://dl-cdn.alpinelinux.org/alpine/edge/main/x86_64/apk-tools-static-3.0.8-r0.apk &&
  tar -xzf apk.static.apk sbin/apk.static && ./sbin/apk.static adbdump --format json packages.adb`
  (Alpine's .apk is still a v2 tar.gz, so plain `tar` extracts it; the binary is static and runs on
  glibc hosts). Same command works on individual `.apk` files. This is the only way to get
  **installed size** (bytes, uncompressed) per package — use it for GUI stack sizing / ramdisk budgets.
- Which feed a package is in is not guessable: the whole GUI tier (cog, libwpewebkit, weston, cage,
  libsdl2, mesa variants) is in the **`video`** feed, which only exists from 25.12 on and is built
  per-arch — always list the feed dir before claiming absence.
- Kernel modules live per target: `.../releases/<rel>/targets/<target>/<subtarget>/kmods/<kver>-<hash>/` — the hash is discoverable by fetching the `kmods/` dir listing. Fetch `index.json` there for kmod names+versions (valid JSON: `{"packages": {...}}`).
- Main-tree packages (uqmi, umbim, comgt, wwan, iproute2...) appear in the **target** packages dir, not the arch feed: `.../targets/<target>/<subtarget>/packages/`.

## 2. Where a kmod/package is defined (source truth)
- All generic kmods: `https://raw.githubusercontent.com/openwrt/openwrt/<ref>/package/kernel/linux/modules/*.mk` (`<ref>` = master / v25.12.5 / v24.10.8).
- Pitfall: looping `curl | python-grep` over many files silently produced false negatives once; re-verify any negative with a second method (single-file `curl | grep -i`, or a local clone).
- Fast local tree for greps: `git clone --filter=blob:none --depth=1 --no-checkout <repo> && git sparse-checkout set <paths> && git checkout` — plain `git grep` on a blobless clone is slow/hangs (fetches blobs on demand).

## 3. Feature-history dates / "since which version"
- Per-file history: `curl -s 'https://api.github.com/repos/openwrt/<repo>/commits?path=<path>&per_page=20'` -> author date + message.
- Keyword search: `curl -s -H 'Accept: application/vnd.github.cloak-preview' 'https://api.github.com/search/commits?q=repo:openwrt/openwrt+<kw>&per_page=10'`.
- Branch-by-branch diff to pin the release: fetch the same file from `openwrt-21.02`, `-22.03`, `-23.05`, `-24.10`, `-25.12` and diff the relevant option/version line.
- lede-commits archive shows full commit bodies + diffs: `https://lists.infradead.org/pipermail/lede-commits/<YYYY>-<Month>/` (search via web_search).
- OpenWrt release changelogs list **sub-repo commits too** (`.../releases/25.12/changelog-25.12.0`): a commit listed there (e.g. "add uqmid") may exist only in the upstream tool's git repo, NOT as a packaged OpenWrt package — always cross-check the tree/index.

## 4. Upstream feature versions (libqmi / ModemManager / kernel)
- NEWS greps: download the whole NEWS, then map each hit to its version with a regex over `Overview of changes in libqmi X.Y` / `ModemManager X.Y.Z` headers. Bare `grep -i` misattributes version blocks.
- Kernel gating: `curl -s https://raw.githubusercontent.com/torvalds/linux/<tag>/<path> | grep -c '<SYMBOL>'` across tags (v6.15..v6.18) tells which release really contains a feature; a LWN article about a patch series does NOT mean merged (check patch revisions on patchew/lore).
- Patch status: `https://patchew.org/search?q=<series title>` lists v1..vN; `https://patchwork.kernel.org/api/1.2/patches/?q=<query>` gives state (superseded / changes-requested / accepted).

## 5. Blocked sources -> workarounds
- Discourse forum (`forum.openwrt.org/search.json`) answers `Crawler is not allowed!`; the HTML search page is rate-limited in the browser. Use `web_search` with `site:forum.openwrt.org <terms>` and read threads with web_extract.
- `grep.app` is behind a Vercel checkpoint — use GitHub API / local clone instead.
- OpenWrt wiki (DokuWiki) fetches fine with curl; `lib/exe/ajax.php?call=search` is not available ("AJAX call 'search' unknown"). Wiki "Last modified:" in the footer is the citable date; `pkgdata` pages are auto-generated and often stale by years — never cite them for current versions.
- `web_extract` (keyless) times out often; fall back to `curl | sed 's/<[^>]*>/ /g'`.

## Reusable verification results
See `references/cellular-modem-2026-findings.md` for the checked 2026 baseline (package versions, kmod definitions, proto transports, upstream feature versions) — reuse or re-verify it instead of re-deriving from scratch.
