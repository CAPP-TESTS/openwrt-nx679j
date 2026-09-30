# LICENSES/ — testi integrali delle licenze dei componenti di terzi

Qui stanno i testi completi che gli elenchi di attribuzione citano. `THIRD_PARTY_LICENSES.md` dice
*quale* componente usa *quale* licenza; questo file dice *dove sta il testo* e da dove è stato preso.

| File | Licenza | Preso da | sha256 (primi 12) |
|---|---|---|---|
| `Apache-2.0.txt` | Apache-2.0 | https://www.apache.org/licenses/LICENSE-2.0.txt | `cfc7749b96f6` |
| `BSD-3-Clause.txt` | BSD-3-Clause | copia da `09-albero-originale/experiments-extra/refs/qrtr-0.3/LICENSE` (Sony Mobile Communications) | `e04f492e71bc` |
| `GPL-2.0.txt` | GPL-2.0 | https://raw.githubusercontent.com/spdx/license-list-data/main/text/GPL-2.0-only.txt | `aaf135472f81` |
| `LGPL-2.1.txt` | LGPL-2.1 | https://raw.githubusercontent.com/spdx/license-list-data/main/text/LGPL-2.1-only.txt | `5749785c8bde` |
| `musl-COPYRIGHT.txt` | musl-COPYRIGHT | https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT | `b870108ec5e7` |
| `svg-spinners-MIT.txt` | svg-spinners-MIT | https://raw.githubusercontent.com/n3r4zzurr0/svg-spinners/main/LICENSE | `712f8f614e9a` |

## Quale componente richiede quale testo

| Componente (vedi `THIRD_PARTY_LICENSES.md` §1) | Testo da includere |
|---|---|
| Sorgenti display vendor (`techpack`, `idlepc_research`, `mainline`), `ipa-lineage20/ipa.c`, `mm-*.c` (ModemManager), script OpenWrt dentro `etc.tar`, `qti-patches` | `GPL-2.0.txt` |
| Sorgenti `libqmi` per confronto (`libqmi-*.c`, `qmi-enums*.h`) | `LGPL-2.1.txt` |
| LuCI (dentro `luci.tar`), `init.rc` di AOSP, header di boot image AOSP | `Apache-2.0.txt` |
| `qrtr`, `rmtfs`, `pd-mapper`, `tqftpserv`, `qdlrs`, estratti `linux-5.10-ns.c` | `BSD-3-Clause.txt` |
| **Binari del progetto** compilati staticamente con musl (25 dei 46 eseguibili) | `musl-COPYRIGHT.txt` |
| Binari statici con glibc (`tools.tar`: `rmtfs`, `tqftpserv`, `pd-mapper`; e `ui-preview`, `rmnet-inspect-host`, `test-qmi-qrtr-host`) | `LGPL-2.1.txt` (vedi nota sui binari statici in `THIRD_PARTY_LICENSES.md` §1) |
| Icona `www/luci-static/resources/icons/loading.svg` dentro `luci.tar` | `svg-spinners-MIT.txt` |
| Documentazione del progetto | `LICENSE-docs` (CC BY 4.0, testo integrale già nel pacchetto) |
| Codice del progetto | `LICENSE` (MIT) |

---

Testi scaricati il **01/10/2026** dalle fonti indicate e verificati con `sha256sum`; gnu.org non era
raggiungibile da questa macchina e per i due testi GNU è stata usata la copia di riferimento SPDX
(`spdx/license-list-data`), che è identica al testo ufficiale.
