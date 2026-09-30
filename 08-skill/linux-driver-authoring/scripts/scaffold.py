#!/usr/bin/env python3
"""Genera lo scheletro di un driver Linux che compila al primo colpo.

Il boilerplate di un driver e' identico ogni volta e sbagliarlo costa tempo in
modi noiosi: una firma di `remove` cambiata fra versioni, un `MODULE_DEVICE_TABLE`
dimenticato, un error path che non srotola. Questo lo genera corretto, cosi' il
lavoro parte dalla logica del dispositivo invece che dalla forma.

Il codice prodotto segue le convenzioni descritte in SKILL.md: risorse con
`devm_*`, `dev_err_probe`, `IRQ_NONE` sugli IRQ condivisi, remove che zittisce
l'hardware, tab per l'indentazione.

Uso:
  scaffold.py --name mydev --bus platform --vendor acme [--irq] [--dt] -o ./out
  scaffold.py --name mychip --bus i2c --regmap -o ./out
  scaffold.py --name mycard --bus pci --vid 0x1234 --pid 0x5678 --dma -o ./out
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

BUSES = ("platform", "pci", "i2c", "spi", "usb")


def c_ident(name: str) -> str:
    ident = re.sub(r"[^0-9a-zA-Z_]", "_", name).lower()
    return ident if not ident[:1].isdigit() else f"drv_{ident}"


def header(a) -> str:
    inc = [
        "#include <linux/device.h>",
        "#include <linux/kernel.h>",
        "#include <linux/module.h>",
    ]
    inc += {
        "platform": ["#include <linux/io.h>", "#include <linux/mod_devicetable.h>",
                     "#include <linux/platform_device.h>"],
        "pci": ["#include <linux/pci.h>", "#include <linux/io.h>"],
        "i2c": ["#include <linux/i2c.h>", "#include <linux/mod_devicetable.h>"],
        "spi": ["#include <linux/spi/spi.h>", "#include <linux/mod_devicetable.h>"],
        "usb": ["#include <linux/usb.h>"],
    }[a.bus]
    if a.irq:
        inc.append("#include <linux/interrupt.h>")
    if a.regmap:
        inc.append("#include <linux/regmap.h>")
    if a.dma:
        inc.append("#include <linux/dma-mapping.h>")
    if a.clk:
        inc.append("#include <linux/clk.h>")
    if a.pm:
        inc.append("#include <linux/pm.h>")
    return "\n".join(sorted(set(inc)))


def priv_struct(a, n: str) -> str:
    fields = ["\tstruct device\t\t*dev;"]
    if a.bus in ("platform", "pci"):
        fields.append("\tvoid __iomem\t\t*base;")
    if a.bus == "i2c":
        fields.append("\tstruct i2c_client\t*client;")
    if a.bus == "spi":
        fields.append("\tstruct spi_device\t*spi;")
    if a.bus == "usb":
        fields += ["\tstruct usb_device\t*udev;", "\tu8\t\t\tbulk_in;", "\tu8\t\t\tbulk_out;"]
    if a.regmap:
        fields.append("\tstruct regmap\t\t*regmap;")
    if a.clk:
        fields.append("\tstruct clk\t\t*clk;")
    if a.irq:
        fields.append("\tint\t\t\tirq;")
    if a.dma:
        fields += ["\tvoid\t\t\t*dma_buf;", "\tdma_addr_t\t\tdma_handle;"]
    fields.append("\t/* protegge l'accesso concorrente allo stato del dispositivo */")
    fields.append("\tspinlock_t\t\tlock;")
    return f"struct {n}_priv {{\n" + "\n".join(fields) + "\n};"


def regs(n: str) -> str:
    N = n.upper()
    return f"""/* Mappa dei registri — sostituisci con quella ricostruita dall'analisi.
 * Ogni valore qui dentro dovrebbe essere tracciabile a un fatto osservato.
 */
#define {N}_REG_ID\t\t0x00
#define {N}_REG_CTRL\t\t0x04
#define  {N}_CTRL_ENABLE\tBIT(0)
#define  {N}_CTRL_RESET\t\tBIT(31)
#define {N}_REG_STATUS\t\t0x08
#define  {N}_STATUS_READY\tBIT(0)
#define {N}_REG_INT_MASK\t\t0x0c
#define {N}_REG_INT_STATUS\t0x10"""


def irq_handler(a, n: str) -> str:
    N = n.upper()
    if a.bus in ("platform", "pci"):
        return f"""
static irqreturn_t {n}_irq(int irq, void *data)
{{
\tstruct {n}_priv *priv = data;
\tu32 status;

\tstatus = readl(priv->base + {N}_REG_INT_STATUS);
\tif (!status)
\t\treturn IRQ_NONE;\t/* IRQ condiviso: non e' nostro */

\t/* ack write-1-to-clear prima di elaborare, per non perdere eventi */
\twritel(status, priv->base + {N}_REG_INT_STATUS);

\t/* TODO: lavoro leggero qui; il resto in threaded IRQ o workqueue */

\treturn IRQ_HANDLED;
}}
"""
    # Su I2C e SPI leggere lo status richiede una transazione sul bus, che puo'
    # dormire: vietata in hard IRQ. Serve quindi un handler in contesto thread.
    return f"""
static irqreturn_t {n}_irq_thread(int irq, void *data)
{{
\tstruct {n}_priv *priv = data;

\t/* Contesto thread: qui dormire e' lecito, quindi si puo' parlare al bus.
\t * TODO: leggi lo status del dispositivo e gestisci l'evento.
\t */
\tdev_dbg(priv->dev, "interrupt ricevuto\\n");

\treturn IRQ_HANDLED;
}}
"""


def hw_init(a, n: str) -> str:
    N = n.upper()
    if a.bus not in ("platform", "pci"):
        return f"""
static int {n}_hw_init(struct {n}_priv *priv)
{{
\t/* TODO: sequenza di init ricostruita dall'analisi del dispositivo */
\treturn 0;
}}
"""
    return f"""
static int {n}_hw_init(struct {n}_priv *priv)
{{
\tu32 val;
\tint ret;

\t/* Reset: alza il bit, attendi, abbassalo. Il ritardo va preso dalla
\t * sequenza osservata, non inventato.
\t */
\twritel({N}_CTRL_RESET, priv->base + {N}_REG_CTRL);
\tudelay(50);
\twritel(0, priv->base + {N}_REG_CTRL);

\tret = readl_poll_timeout(priv->base + {N}_REG_STATUS, val,
\t\t\t\t val & {N}_STATUS_READY, 100, 100000);
\tif (ret) {{
\t\tdev_err(priv->dev, "il dispositivo non diventa ready dopo il reset\\n");
\t\treturn ret;
\t}}

\twritel({N}_CTRL_ENABLE, priv->base + {N}_REG_CTRL);

\treturn 0;
}}
"""


def probe_body(a, n: str) -> str:
    N = n.upper()
    L = []
    if a.bus == "platform":
        L += ["\tstruct device *dev = &pdev->dev;", f"\tstruct {n}_priv *priv;", "\tint ret;", ""]
    elif a.bus == "pci":
        L += ["\tstruct device *dev = &pdev->dev;", f"\tstruct {n}_priv *priv;", "\tint ret;", ""]
    elif a.bus == "i2c":
        L += ["\tstruct device *dev = &client->dev;", f"\tstruct {n}_priv *priv;",
              "\tint ret;", ""]
    elif a.bus == "spi":
        L += ["\tstruct device *dev = &spi->dev;", f"\tstruct {n}_priv *priv;", "\tint ret;", ""]
    elif a.bus == "usb":
        L += ["\tstruct device *dev = &intf->dev;",
              "\tstruct usb_endpoint_descriptor *bulk_in, *bulk_out;",
              f"\tstruct {n}_priv *priv;", "\tint ret;", ""]

    if a.bus == "pci":
        L += ["\tret = pcim_enable_device(pdev);", "\tif (ret)", "\t\treturn ret;", ""]

    L += ["\tpriv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);",
          "\tif (!priv)", "\t\treturn -ENOMEM;", "",
          "\tpriv->dev = dev;", "\tspin_lock_init(&priv->lock);", ""]

    if a.bus == "platform":
        L += ["\tpriv->base = devm_platform_ioremap_resource(pdev, 0);",
              "\tif (IS_ERR(priv->base))", "\t\treturn PTR_ERR(priv->base);", ""]
    elif a.bus == "pci":
        L += ["\tret = pcim_iomap_regions(pdev, BIT(0), KBUILD_MODNAME);",
              "\tif (ret)", "\t\treturn ret;",
              "\tpriv->base = pcim_iomap_table(pdev)[0];", ""]
    elif a.bus == "i2c":
        L += ["\tpriv->client = client;", ""]
    elif a.bus == "spi":
        L += ["\tpriv->spi = spi;", ""]
    elif a.bus == "usb":
        L += ["\tpriv->udev = usb_get_dev(interface_to_usbdev(intf));", "",
              "\tret = usb_find_common_endpoints(intf->cur_altsetting,",
              "\t\t\t\t\t&bulk_in, &bulk_out, NULL, NULL);",
              "\tif (ret) {",
              "\t\tusb_put_dev(priv->udev);",
              '\t\treturn dev_err_probe(dev, ret, "endpoint bulk mancanti\\n");',
              "\t}",
              "\tpriv->bulk_in = bulk_in->bEndpointAddress;",
              "\tpriv->bulk_out = bulk_out->bEndpointAddress;", ""]

    if a.regmap:
        init = {"i2c": "devm_regmap_init_i2c(client", "spi": "devm_regmap_init_spi(spi"}.get(
            a.bus, "devm_regmap_init_mmio(dev, priv->base")
        L += [f"\tpriv->regmap = {init}, &{n}_regmap_config);",
              "\tif (IS_ERR(priv->regmap))",
              "\t\treturn dev_err_probe(dev, PTR_ERR(priv->regmap),",
              '\t\t\t\t     "init regmap fallita\\n");', ""]

    if a.clk:
        L += ["\tpriv->clk = devm_clk_get_enabled(dev, NULL);",
              "\tif (IS_ERR(priv->clk))",
              "\t\treturn dev_err_probe(dev, PTR_ERR(priv->clk),",
              '\t\t\t\t     "clock non disponibile\\n");', ""]

    if a.dma and a.bus == "pci":
        L += ["\tret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));",
              "\tif (ret) {",
              "\t\tret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));",
              "\t\tif (ret)",
              '\t\t\treturn dev_err_probe(dev, ret, "nessuna maschera DMA usabile\\n");',
              "\t}",
              "\tpci_set_master(pdev);\t/* obbligatorio prima di qualsiasi DMA */", ""]

    if a.irq:
        if a.bus == "platform":
            L += ["\tpriv->irq = platform_get_irq(pdev, 0);",
                  "\tif (priv->irq < 0)", "\t\treturn priv->irq;", ""]
        elif a.bus == "pci":
            L += ["\tret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);",
                  "\tif (ret < 0)", "\t\treturn ret;",
                  "\tpriv->irq = pci_irq_vector(pdev, 0);", ""]
        elif a.bus in ("i2c", "spi"):
            L += [f"\tpriv->irq = {'client' if a.bus == 'i2c' else 'spi'}->irq;", ""]
        if a.bus in ("platform", "pci"):
            L += [f"\tret = devm_request_irq(dev, priv->irq, {n}_irq, IRQF_SHARED,",
                  "\t\t\t       KBUILD_MODNAME, priv);",
                  "\tif (ret)",
                  '\t\treturn dev_err_probe(dev, ret, "richiesta IRQ fallita\\n");', ""]
        elif a.bus in ("i2c", "spi"):
            # Handler threaded: su questi bus leggere lo status significa fare
            # una transazione, e una transazione puo' dormire.
            L += ["\tret = devm_request_threaded_irq(dev, priv->irq, NULL,",
                  f"\t\t\t\t\t{n}_irq_thread,",
                  "\t\t\t\t\tIRQF_ONESHOT, KBUILD_MODNAME, priv);",
                  "\tif (ret)",
                  '\t\treturn dev_err_probe(dev, ret, "richiesta IRQ fallita\\n");', ""]

    # hw_init viene generato per ogni bus: chiamarlo sempre evita sia il warning
    # -Wunused-function sia il dubbio su dove vada la sequenza di init.
    if a.bus == "usb":
        L += [f"\tret = {n}_hw_init(priv);",
              "\tif (ret) {",
              "\t\tusb_put_dev(priv->udev);",
              "\t\treturn ret;",
              "\t}", ""]
    else:
        L += [f"\tret = {n}_hw_init(priv);", "\tif (ret)", "\t\treturn ret;", ""]

    setter = {
        "platform": "platform_set_drvdata(pdev, priv);",
        "pci": "pci_set_drvdata(pdev, priv);",
        "i2c": "i2c_set_clientdata(client, priv);",
        "spi": "spi_set_drvdata(spi, priv);",
        "usb": "usb_set_intfdata(intf, priv);",
    }[a.bus]
    L += [f"\t{setter}", "",
          '\tdev_info(dev, "probe completato\\n");', "", "\treturn 0;"]
    return "\n".join(L)


def driver_c(a) -> str:
    n = c_ident(a.name)
    N = n.upper()
    compat = f"{a.vendor},{a.name}"

    parts = [f"// SPDX-License-Identifier: {a.license}", header(a), ""]
    if a.bus in ("platform", "pci"):
        parts += ["#include <linux/iopoll.h>", "#include <linux/delay.h>", ""]
    parts += [regs(n), "", priv_struct(a, n), ""]

    if a.regmap:
        parts += [f"""static const struct regmap_config {n}_regmap_config = {{
\t.reg_bits\t= 8,
\t.val_bits\t= 8,
\t.max_register\t= 0xff,
\t/* TODO: dichiara volatili status, FIFO e registri clear-on-read,
\t * altrimenti la cache restituisce valori vecchi.
\t */
}};
"""]

    if a.irq and a.bus != "usb":
        parts.append(irq_handler(a, n))
    parts.append(hw_init(a, n))

    sig = {
        "platform": f"static int {n}_probe(struct platform_device *pdev)",
        "pci": f"static int {n}_probe(struct pci_dev *pdev, const struct pci_device_id *id)",
        "i2c": f"static int {n}_probe(struct i2c_client *client)",
        "spi": f"static int {n}_probe(struct spi_device *spi)",
        "usb": f"static int {n}_probe(struct usb_interface *intf, const struct usb_device_id *id)",
    }[a.bus]
    parts += [f"\n{sig}\n{{\n{probe_body(a, n)}\n}}\n"]

    # remove / disconnect
    quiet = (f"\twritel(0, priv->base + {N}_REG_INT_MASK);"
             if a.bus in ("platform", "pci") else "\t/* TODO: metti l'hardware in stato quiescente */")
    note = ("\t/* devm libera le risorse; qui si zittisce l'hardware, che devm non puo' fare */")
    if a.bus == "platform":
        parts += [f"""static void {n}_remove(struct platform_device *pdev)
{{
\tstruct {n}_priv *priv = platform_get_drvdata(pdev);

{note}
{quiet}
}}
"""]
    elif a.bus == "pci":
        parts += [f"""static void {n}_remove(struct pci_dev *pdev)
{{
\tstruct {n}_priv *priv = pci_get_drvdata(pdev);

{note}
{quiet}
}}
"""]
    elif a.bus == "usb":
        parts += [f"""static void {n}_disconnect(struct usb_interface *intf)
{{
\tstruct {n}_priv *priv = usb_get_intfdata(intf);

\tusb_set_intfdata(intf, NULL);
\t/* TODO: usb_kill_urb() su ogni URB in volo PRIMA di liberare qualsiasi cosa */
\tusb_put_dev(priv->udev);
}}
"""]
    else:
        parts += [f"""static void {n}_remove(struct {'i2c_client *client' if a.bus == 'i2c' else 'spi_device *spi'})
{{
{note}
}}
"""]

    if a.pm and a.bus in ("platform", "pci", "i2c", "spi"):
        parts += [f"""static int __maybe_unused {n}_suspend(struct device *dev)
{{
\t/* TODO: disattiva interrupt e clock */
\treturn 0;
}}

static int __maybe_unused {n}_resume(struct device *dev)
{{
\t/* Dopo il resume i registri non sono conservati: riprogramma da capo. */
\treturn 0;
}}

static SIMPLE_DEV_PM_OPS({n}_pm_ops, {n}_suspend, {n}_resume);
"""]
    pm_ref = f"\n\t\t.pm\t\t= &{n}_pm_ops," if (a.pm and a.bus in ("platform", "pci", "i2c", "spi")) else ""

    # match table + driver struct
    if a.bus == "platform":
        parts += [f"""static const struct of_device_id {n}_of_match[] = {{
\t{{ .compatible = "{compat}" }},
\t{{ }}
}};
MODULE_DEVICE_TABLE(of, {n}_of_match);

static struct platform_driver {n}_driver = {{
\t.probe\t= {n}_probe,
\t.remove\t= {n}_remove,
\t.driver\t= {{
\t\t.name\t\t= KBUILD_MODNAME,
\t\t.of_match_table\t= {n}_of_match,{pm_ref}
\t}},
}};
module_platform_driver({n}_driver);
"""]
    elif a.bus == "pci":
        parts += [f"""static const struct pci_device_id {n}_pci_ids[] = {{
\t{{ PCI_DEVICE({a.vid}, {a.pid}) }},
\t{{ }}
}};
MODULE_DEVICE_TABLE(pci, {n}_pci_ids);

static struct pci_driver {n}_driver = {{
\t.name\t\t= KBUILD_MODNAME,
\t.id_table\t= {n}_pci_ids,
\t.probe\t\t= {n}_probe,
\t.remove\t\t= {n}_remove,
}};
module_pci_driver({n}_driver);
"""]
    elif a.bus == "i2c":
        parts += [f"""static const struct of_device_id {n}_of_match[] = {{
\t{{ .compatible = "{compat}" }},
\t{{ }}
}};
MODULE_DEVICE_TABLE(of, {n}_of_match);

static const struct i2c_device_id {n}_id[] = {{
\t{{ "{a.name}" }},
\t{{ }}
}};
MODULE_DEVICE_TABLE(i2c, {n}_id);

static struct i2c_driver {n}_driver = {{
\t.driver = {{
\t\t.name\t\t= KBUILD_MODNAME,
\t\t.of_match_table\t= {n}_of_match,{pm_ref}
\t}},
\t.probe\t\t= {n}_probe,
\t.remove\t\t= {n}_remove,
\t.id_table\t= {n}_id,
}};
module_i2c_driver({n}_driver);
"""]
    elif a.bus == "spi":
        parts += [f"""static const struct of_device_id {n}_of_match[] = {{
\t{{ .compatible = "{compat}" }},
\t{{ }}
}};
MODULE_DEVICE_TABLE(of, {n}_of_match);

static struct spi_driver {n}_driver = {{
\t.driver = {{
\t\t.name\t\t= KBUILD_MODNAME,
\t\t.of_match_table\t= {n}_of_match,{pm_ref}
\t}},
\t.probe\t\t= {n}_probe,
\t.remove\t\t= {n}_remove,
}};
module_spi_driver({n}_driver);
"""]
    elif a.bus == "usb":
        parts += [f"""static const struct usb_device_id {n}_usb_ids[] = {{
\t{{ USB_DEVICE({a.vid}, {a.pid}) }},
\t{{ }}
}};
MODULE_DEVICE_TABLE(usb, {n}_usb_ids);

static struct usb_driver {n}_driver = {{
\t.name\t\t= KBUILD_MODNAME,
\t.id_table\t= {n}_usb_ids,
\t.probe\t\t= {n}_probe,
\t.disconnect\t= {n}_disconnect,
}};
module_usb_driver({n}_driver);
"""]

    parts += [f"""MODULE_DESCRIPTION("Driver per {compat}");
MODULE_AUTHOR("{a.author}");
MODULE_LICENSE("{a.license}");
"""]
    return "\n".join(parts)


def makefile(n: str) -> str:
    return f"""obj-m += {n}.o

KDIR ?= /lib/modules/$(shell uname -r)/build

# $(CURDIR), non $(PWD): con `make -C altra/dir` il secondo resta la directory da
# cui hai lanciato il comando, e il modulo verrebbe cercato nel posto sbagliato.
MDIR := $(CURDIR)

# Un modulo va compilato con lo stesso compilatore del kernel, altrimenti nella
# migliore delle ipotesi warning, nella peggiore un modulo che non carica.
# Diverse distribuzioni costruiscono il kernel con clang: in quel caso serve LLVM=1.
# Il valore non e' cablato, si legge da CONFIG_CC_IS_CLANG nel .config del kernel.
ifneq ($(wildcard $(KDIR)/.config),)
  ifneq ($(shell grep -c '^CONFIG_CC_IS_CLANG=y' $(KDIR)/.config),0)
    LLVM ?= 1
    export LLVM
  endif
endif

all:
	$(MAKE) -C $(KDIR) M=$(MDIR) modules

# I warning del kernel indicano quasi sempre bug reali: compila sempre con W=1.
strict:
	$(MAKE) -C $(KDIR) M=$(MDIR) W=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) clean

.PHONY: all strict clean
"""


def dt_binding(a, n: str) -> str:
    return f"""%YAML 1.2
---
$id: http://devicetree.org/schemas/{a.vendor}/{a.vendor},{a.name}.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: {a.vendor} {a.name}

maintainers:
  - {a.author}

properties:
  compatible:
    const: {a.vendor},{a.name}
  reg:
    maxItems: 1
{"  interrupts:" if a.irq else ""}
{"    maxItems: 1" if a.irq else ""}
{"  clocks:" if a.clk else ""}
{"    maxItems: 1" if a.clk else ""}

required:
  - compatible
  - reg
{"  - interrupts" if a.irq else ""}
{"  - clocks" if a.clk else ""}

additionalProperties: false

examples:
  - |
    {n}@10010000 {{
        compatible = "{a.vendor},{a.name}";
        reg = <0x10010000 0x1000>;
{'        interrupts = <0 42 4>;' if a.irq else ''}
{'        clocks = <&clk 7>;' if a.clk else ''}
    }};
"""


def main() -> int:
    p = argparse.ArgumentParser(description="Genera uno scheletro di driver Linux compilabile.")
    p.add_argument("--name", required=True, help="nome del driver, es. mydev")
    p.add_argument("--bus", required=True, choices=BUSES)
    p.add_argument("--vendor", default="vendor", help="prefisso vendor per la compatible")
    p.add_argument("--vid", default="0x1234", help="vendor ID per PCI/USB")
    p.add_argument("--pid", default="0x5678", help="product/device ID per PCI/USB")
    p.add_argument("--author", default="Nome Cognome <email@example.com>")
    p.add_argument("--license", default="GPL", choices=("GPL", "GPL v2", "Dual BSD/GPL"))
    p.add_argument("--irq", action="store_true", help="gestione interrupt")
    p.add_argument("--dma", action="store_true", help="setup DMA (PCI)")
    p.add_argument("--clk", action="store_true", help="clock")
    p.add_argument("--regmap", action="store_true", help="usa regmap")
    p.add_argument("--pm", action="store_true", help="suspend/resume")
    p.add_argument("--dt", action="store_true", help="genera anche il binding YAML")
    p.add_argument("-o", "--out", default=".", help="directory di output")
    a = p.parse_args()

    n = c_ident(a.name)
    out = Path(a.out).expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)

    written = []
    (out / f"{n}.c").write_text(driver_c(a), encoding="utf-8")
    written.append(f"{n}.c")
    (out / "Makefile").write_text(makefile(n), encoding="utf-8")
    written.append("Makefile")
    if a.dt:
        fn = f"{a.vendor},{a.name}.yaml"
        (out / fn).write_text(dt_binding(a, n), encoding="utf-8")
        written.append(fn)

    print(f"Generato in {out}:")
    for f in written:
        print(f"  {f}")
    print(f"\nCompila con:  make -C {out} strict")
    print("Poi valida con lo script kcheck.sh della skill kernel-debug-validate.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
