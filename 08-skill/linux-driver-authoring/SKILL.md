---
name: linux-driver-authoring
description: Scrive driver kernel Linux di qualità mainline — moduli, platform/PCI/USB/I2C/SPI driver, character device, binding Device Tree, gestione di IRQ, DMA, clock, regolatori, locking e power management, con Kconfig/Makefile e build out-of-tree. Usa questa skill ogni volta che si deve scrivere, portare, correggere o rivedere codice kernel Linux o un modulo .ko, anche solo per un driver minimale o per un esempio didattico. Copre error path con devm_*, Device Tree binding YAML e integrazione nel sottosistema corretto.
version: 1.0.0
license: MIT
metadata:
  hermes:
    tags: [linux-kernel, driver, device-tree, pci, usb, i2c, spi, dma, kernel-module]
---

# Linux driver authoring

Scrivere un driver è per gran parte un lavoro di scelte: quale sottosistema, quale bus, quale ciclo di vita, quale strategia di locking. Il codice viene dopo, ed è la parte facile. Le scelte sbagliate non si notano al primo `insmod`: si notano sotto carico, in sospensione, o quando qualcuno stacca il dispositivo mentre è in uso.

## Prima di scrivere: quattro domande

**1. Quale sottosistema?** Un driver che entra nel sottosistema giusto eredita gratis interfaccia userspace, sysfs, power management e testing. Le famiglie principali:

`iio` per sensori e ADC/DAC · `input` per tastiere, touch, joystick · `hwmon` per temperature, tensioni, ventole · `leds` per LED · `pwm` · `rtc` · `watchdog` · `net`/`wireless` · `media`/V4L2 per video · `sound`/ALSA · `mtd` per flash · `block` · `iommu` · `gpio` e `pinctrl` · `regulator` · `clk` · `thermal` · `power_supply` per batterie.

Un character device custom è la scelta corretta solo quando il dispositivo davvero non appartiene a nessuna categoria esistente: è la risposta rara, non quella comoda.

**2. Quale bus?** Determina la struttura del driver e il meccanismo di matching:

- Memory-mapped su SoC, descritto da Device Tree → `platform_driver` + `of_match_table`.
- PCI/PCIe → `pci_driver` + `pci_device_id`.
- USB → `usb_driver` + `usb_device_id`.
- I2C → `i2c_driver`; SPI → `spi_driver`. Entrambi valutano `regmap`.
- Piattaforme x86 con firmware ACPI → matching via `acpi_match_table`.

**3. Regmap è applicabile?** Se il dispositivo espone registri su I2C, SPI o MMIO, `regmap` fornisce gratuitamente caching, campi bit, endianness, locking e debugfs. Scrivere accessi a mano quando `regmap` è applicabile significa riscrivere codice già testato da migliaia di driver.

**4. Esiste un driver simile?** Cerca nel tree per chip della stessa famiglia. Copiare la struttura di un driver esistente e adattarla è il percorso più affidabile, non una scorciatoia.

**Regola assoluta dell'utente (2026-09-20): la ricerca di prior art viene PRIMA di scrivere codice.** Non basta guardare il build tree locale: il driver (o parte della sua infrastruttura) può esistere fuori — upstream, in review sulle liste, in un fork vendor, in un altro progetto. Cerca in quest'ordine:

1. **Upstream a tutto campo**: elixir.bootlin.com per l'albero, lore.kernel.org per patch e discussioni, patchwork per le serie in review (un driver "non upstream" può essere a una serie dalla merge!).
2. **Fork e progetti vicini**: driver vendor di altri prodotti/SoC, fork community (es. per Qualcomm: linux-msm, postmarketOS, fork device-specific).
3. **L'infrastruttura del sottosistema**: prima di scrivere da zero controlla se il sottosistema giusto ha già helper/API per il tuo caso (netlink, sysfs, uclass...).

Esito possibile: *adotto* | *adatto (documentando le modifiche)* | *scrivo ex novo perché X* (con prova dell'assenza). Caso reale (NX679J): un intero client QMI-over-QRTR scritto a mano quando `qmicli`/`libqmi` con QRTR era pacchettizzato e funzionante — scoperto dopo. Il codice custom è la risposta rara, e va giustificata.

Cerca nel build tree locale:

```bash
K=/usr/lib/modules/$(uname -r)/build
grep -rl "compatible.*<vendor>" "$K/include/dt-bindings" 2>/dev/null
ls "$K/include/linux/" | grep -i <sottosistema>
```

Per i sorgenti completi dei driver esistenti serve l'albero del kernel (`linux-source`, o https://elixir.bootlin.com per consultazione rapida): il build tree contiene header e script, non i `.c`.

## Regole non negoziabili

Sono poche e hanno tutte la stessa origine: nel kernel non c'è nessuno che raccolga i cocci.

**Risorse con `devm_*`.** `devm_kzalloc`, `devm_ioremap_resource`, `devm_clk_get`, `devm_request_irq`, `devm_regulator_get`: liberate automaticamente quando il device viene distrutto, in ordine inverso. Eliminano la classe di bug più comune nei driver, cioè l'error path che dimentica una `free`.

L'eccezione va conosciuta: se una risorsa deve sopravvivere oltre `remove()`, o se l'ordine di rilascio deve intrecciarsi con la disattivazione dell'hardware, `devm_*` non basta. In quel caso l'error path esplicito con `goto` in ordine inverso è la forma corretta.

**Contesto atomico.** In interrupt handler, sotto spinlock o in RCU read-side: niente `msleep`, niente `mutex_lock`, niente allocazioni `GFP_KERNEL`. Solo `GFP_ATOMIC`, `udelay`, `spin_lock`. La violazione non fallisce subito: fallisce sotto carico, in modo apparentemente casuale.

**Niente floating point.** Il kernel non salva lo stato FPU nei context switch normali. Usa aritmetica a interi con fattori di scala.

**Validazione dell'input da userspace.** Ogni valore che arriva da `copy_from_user`, sysfs o ioctl è ostile finché non è validato: bounds, overflow nelle moltiplicazioni, dimensioni coerenti. E il classico: fra la validazione e l'uso il valore non deve poter cambiare (niente doppie letture dal buffer utente).

**Niente `printk` in produzione.** Usa `dev_err`, `dev_warn`, `dev_info`, `dev_dbg`: legano il messaggio al device, che è l'informazione che serve a chi legge `dmesg` con dieci dispositivi collegati.

## Generare lo scheletro

Il boilerplate non si scrive a mano:

```bash
scripts/scaffold.py --name mydev --bus platform --vendor acme --irq --clk --pm --dt -o ./mydev
scripts/scaffold.py --name mychip --bus i2c --regmap --irq -o ./mychip
scripts/scaffold.py --name mycard --bus pci --vid 0x1234 --pid 0x5678 --irq --dma -o ./mycard
```

Genera sorgente, Makefile e, con `--dt`, il binding YAML. Tutti e cinque i bus producono codice che compila con `W=1` senza warning e passa `checkpatch --strict` senza rilievi.

Due dettagli che lo script gestisce e che a mano si sbagliano spesso: il Makefile usa `$(CURDIR)` invece di `$(PWD)`, e rileva se il kernel e' stato costruito con clang impostando `LLVM=1` di conseguenza. Su I2C e SPI genera un handler IRQ *threaded*, perche' su quei bus leggere lo status richiede una transazione che puo' dormire.

Lo scheletro e' un punto di partenza corretto nella forma, non nel contenuto: la mappa dei registri e le sequenze vanno sostituite con quelle ricostruite dall'analisi.

## Struttura di un platform driver

Il template minimo ma completo, che copre il 90% dei casi su SoC:

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define MYDEV_REG_CTRL		0x00
#define MYDEV_REG_STATUS	0x04
#define  MYDEV_STATUS_READY	BIT(0)
#define MYDEV_REG_INT_MASK	0x08

struct mydev {
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	int			irq;
	spinlock_t		lock;	/* protegge l'accesso concorrente ai registri */
};

static irqreturn_t mydev_irq(int irq, void *data)
{
	struct mydev *priv = data;
	u32 status;

	status = readl(priv->base + MYDEV_REG_STATUS);
	if (!status)
		return IRQ_NONE;	/* IRQ condiviso: non è nostro */

	writel(status, priv->base + MYDEV_REG_STATUS);	/* ack write-1-to-clear */

	return IRQ_HANDLED;
}

static int mydev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mydev *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	spin_lock_init(&priv->lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "clock non disponibile\n");

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	ret = devm_request_irq(dev, priv->irq, mydev_irq, 0, dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret, "richiesta IRQ fallita\n");

	platform_set_drvdata(pdev, priv);

	return 0;
}

static void mydev_remove(struct platform_device *pdev)
{
	struct mydev *priv = platform_get_drvdata(pdev);

	writel(0, priv->base + MYDEV_REG_INT_MASK);	/* zittisci l'hardware */
}

static const struct of_device_id mydev_of_match[] = {
	{ .compatible = "vendor,mydevice" },
	{ }
};
MODULE_DEVICE_TABLE(of, mydev_of_match);

static struct platform_driver mydev_driver = {
	.probe	= mydev_probe,
	.remove	= mydev_remove,
	.driver	= {
		.name		= "mydev",
		.of_match_table	= mydev_of_match,
	},
};
module_platform_driver(mydev_driver);

MODULE_DESCRIPTION("Driver per vendor,mydevice");
MODULE_AUTHOR("...");
MODULE_LICENSE("GPL");
```

Dettagli che contano in questo template:

- **`dev_err_probe()`** invece di `dev_err()` + `return ret`: gestisce `-EPROBE_DEFER` senza inondare il log quando una dipendenza non è ancora pronta. È l'idioma corrente.
- **`IRQ_NONE`** quando lo status è vuoto: obbligatorio con IRQ condivisi, altrimenti il kernel non riesce a individuare gli interrupt spuri.
- **`remove()` che zittisce l'hardware**: le risorse le libera `devm`, ma il dispositivo va messo in uno stato in cui non genera più interrupt né DMA prima che le strutture spariscano. Questo `devm` non può farlo per te.
- **Firma di `remove`**: dai kernel recenti restituisce `void`. Su kernel più vecchi restituisce `int` — verifica la versione target.
- **`MODULE_DEVICE_TABLE`**: senza, il modulo non viene caricato automaticamente. È la dimenticanza classica.

## Device Tree

Il binding va scritto come schema YAML sotto `Documentation/devicetree/bindings/`, e la `compatible` deve combaciare esattamente con `of_match_table`.

```yaml
%YAML 1.2
---
$id: http://devicetree.org/schemas/vendor/vendor,mydevice.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Vendor MyDevice controller

maintainers:
  - Nome Cognome <email@example.com>

properties:
  compatible:
    const: vendor,mydevice
  reg:
    maxItems: 1
  interrupts:
    maxItems: 1
  clocks:
    maxItems: 1

required:
  - compatible
  - reg
  - interrupts
  - clocks

additionalProperties: false

examples:
  - |
    mydevice@10010000 {
        compatible = "vendor,mydevice";
        reg = <0x10010000 0x1000>;
        interrupts = <0 42 4>;
        clocks = <&clk 7>;
    };
```

Il nodo corrispondente:

```dts
mydevice@10010000 {
	compatible = "vendor,mydevice";
	reg = <0x10010000 0x1000>;
	interrupts = <GIC_SPI 42 IRQ_TYPE_LEVEL_HIGH>;
	clocks = <&clk_gate 7>;
	pinctrl-names = "default";
	pinctrl-0 = <&mydevice_pins>;
	status = "okay";
};
```

Principi: l'hardware si descrive nel Device Tree, non si codifica nel driver — pin, clock, interrupt, regolatori, GPIO. Il driver legge ciò che il DT dichiara. Un driver che contiene numeri di GPIO o indirizzi fissi funziona su una board e su nessun'altra.

Validazione locale:

```bash
dtc -I dts -O dtb -o /tmp/test.dtb board.dts     # sintassi
```

## Concorrenza

La scelta della primitiva discende dal contesto, non dal gusto:

- **`mutex`** — contesto che può dormire. È la scelta predefinita per la configurazione, l'accesso da sysfs, i percorsi non critici.
- **`spinlock`** — sezioni brevissime condivise con contesto atomico. Con un interrupt handler di mezzo serve la variante `_irqsave` sul lato processo, altrimenti si ottiene un deadlock in un istante.
- **`RCU`** — molte letture, poche scritture, letture che devono essere prive di attese.
- **Atomici e `READ_ONCE`/`WRITE_ONCE`** — contatori e flag singoli, dove un lock intero è sproporzionato.

Nell'interrupt handler resta solo il minimo: leggi lo status, fai l'ack, sveglia chi deve lavorare. Il lavoro pesante va in un threaded IRQ (`devm_request_threaded_irq`), in una workqueue o in un tasklet. Un handler lungo aumenta la latenza di tutto il sistema.

L'ordine di acquisizione dei lock va deciso una volta e documentato in un commento vicino alle definizioni. `lockdep` verifica a runtime che tu lo rispetti — vedi `kernel-debug-validate`.

## Power management

```c
static int mydev_suspend(struct device *dev)
{
	struct mydev *priv = dev_get_drvdata(dev);

	writel(0, priv->base + MYDEV_REG_INT_MASK);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int mydev_resume(struct device *dev)
{
	struct mydev *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	return mydev_hw_init(priv);	/* riprogramma: dopo il resume i registri non sono conservati */
}

static DEFINE_SIMPLE_DEV_PM_OPS(mydev_pm_ops, mydev_suspend, mydev_resume);
```

L'assunzione da non fare mai: che il dispositivo conservi il proprio stato durante la sospensione. In generale non lo conserva, e il resume deve riprogrammarlo dall'inizio.

## Build out-of-tree

```makefile
obj-m += mydev.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

Compilazione con i warning attivi, che è l'unico modo sensato di compilare codice kernel:

```bash
make -C /usr/lib/modules/$(uname -r)/build M=$PWD W=1 modules
```

Per il codice destinato al tree serve anche il `Kconfig`:

```
config MYDEV
	tristate "Vendor MyDevice support"
	depends on OF
	help
	  Driver per il controller MyDevice.
	  Compilato come modulo, si chiamerà mydev.
```

## Bus specifici

Per i template di PCI, USB, I2C, SPI, regmap, character device e DMA leggi `references/bus-patterns.md`. Contiene le forme idiomatiche di ciascuno con le trappole tipiche.

## Prima di dire "fatto"

Il codice non è finito quando compila. Passa a `kernel-debug-validate` per il ciclo di verifica: `W=1`, `checkpatch --strict`, sanitizer, load/unload ripetuti e test in QEMU. È lì che si scopre cosa manca davvero.
