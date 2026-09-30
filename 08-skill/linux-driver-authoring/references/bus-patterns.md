# Pattern per bus e sottosistemi

Forme idiomatiche per i bus più comuni, con le trappole che costano più tempo. Leggi solo la sezione che ti serve.

- [PCI/PCIe](#pcipcie)
- [USB](#usb)
- [I2C e SPI](#i2c-e-spi)
- [regmap](#regmap)
- [Character device](#character-device)
- [DMA](#dma)
- [Sysfs e debugfs](#sysfs-e-debugfs)

## PCI/PCIe

```c
static int mypci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mydev *priv;
	int ret;

	ret = pcim_enable_device(pdev);		/* variante devm: niente disable manuale */
	if (ret)
		return ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = pcim_iomap_regions(pdev, BIT(0), KBUILD_MODNAME);	/* solo BAR0 */
	if (ret)
		return ret;
	priv->base = pcim_iomap_table(pdev)[0];

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			return dev_err_probe(&pdev->dev, ret, "nessuna maschera DMA utilizzabile\n");
	}

	pci_set_master(pdev);			/* obbligatorio prima di qualsiasi DMA */

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(&pdev->dev, pci_irq_vector(pdev, 0), mypci_irq,
			       0, KBUILD_MODNAME, priv);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, priv);

	return 0;
}

static const struct pci_device_id mypci_ids[] = {
	{ PCI_DEVICE(0x1234, 0x5678) },
	{ }
};
MODULE_DEVICE_TABLE(pci, mypci_ids);
```

Trappole:

- **`pci_set_master()` dimenticata**: il DMA semplicemente non parte, senza errori espliciti.
- **Maschera DMA impostata dopo l'allocazione dei buffer**: va impostata prima, altrimenti i buffer già allocati possono stare fuori dal range indirizzabile.
- **IRQ legacy condiviso**: l'handler deve restituire `IRQ_NONE` quando lo status è vuoto.
- **`pcim_*` e `pci_*` mescolati**: scegli lo stile devm o quello manuale, non entrambi sullo stesso device.

## USB

```c
static int myusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	struct mydev *priv;
	int ret;

	priv = devm_kzalloc(&intf->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = usb_find_common_endpoints(intf->cur_altsetting,
					&bulk_in, &bulk_out, NULL, NULL);
	if (ret)
		return dev_err_probe(&intf->dev, ret, "endpoint bulk mancanti\n");

	priv->udev = usb_get_dev(udev);
	priv->bulk_in_addr = bulk_in->bEndpointAddress;
	priv->bulk_out_addr = bulk_out->bEndpointAddress;

	usb_set_intfdata(intf, priv);

	return 0;
}

static void myusb_disconnect(struct usb_interface *intf)
{
	struct mydev *priv = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	usb_kill_urb(priv->urb);	/* prima di liberare qualsiasi cosa */
	usb_put_dev(priv->udev);
}
```

Control transfer vendor-specific, la forma che serve quasi sempre nel porting da driver proprietari:

```c
ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
		      bRequest,
		      USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
		      wValue, wIndex, buf, len,
		      USB_CTRL_SET_TIMEOUT);
```

Trappole:

- **Buffer sullo stack per i trasferimenti**: il DMA non può usarli. Alloca con `kmalloc` o usa `usb_control_msg_send()`/`usb_control_msg_recv()`, che gestiscono il bounce buffer.
- **URB non uccisi in `disconnect()`**: use-after-free garantito quando l'URB completa dopo la free.
- **Il dispositivo può sparire in qualsiasi momento**: ogni funzione deve gestire `-ENODEV` e `-ESHUTDOWN`.
- **Short packet**: i trasferimenti bulk possono restituire meno byte del richiesto; controlla sempre la lunghezza effettiva.

## I2C e SPI

```c
static int myi2c_probe(struct i2c_client *client)
{
	struct mydev *priv;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);

	return 0;
}

static const struct of_device_id myi2c_of_match[] = {
	{ .compatible = "vendor,mychip" },
	{ }
};
MODULE_DEVICE_TABLE(of, myi2c_of_match);

static const struct i2c_device_id myi2c_id[] = {
	{ "mychip" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, myi2c_id);
```

SPI segue la stessa forma con `spi_driver`, più i parametri di trasferimento:

```c
struct spi_transfer xfer = {
	.tx_buf		= tx,
	.rx_buf		= rx,
	.len		= len,
	.speed_hz	= 1000000,
	.bits_per_word	= 8,
};
struct spi_message msg;

spi_message_init(&msg);
spi_message_add_tail(&xfer, &msg);
ret = spi_sync(priv->spi, &msg);
```

Trappole:

- **Buffer su stack anche qui**: molti controller SPI usano DMA. Alloca con `kmalloc`.
- **`spi->mode` va impostato in `probe()`** e confermato con `spi_setup()`.
- **I2C non ha un vero errore di lettura**: un dispositivo assente può restituire `0xff` invece di un errore. Verifica sempre un registro ID noto in `probe()`.

## regmap

Quando applicabile, sostituisce decine di righe di accessi manuali:

```c
static const struct regmap_config mychip_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= 0x7f,
	.cache_type	= REGCACHE_MAPLE,
	.volatile_reg	= mychip_volatile_reg,	/* quali registri NON cachare */
};

priv->regmap = devm_regmap_init_i2c(client, &mychip_regmap);
if (IS_ERR(priv->regmap))
	return dev_err_probe(&client->dev, PTR_ERR(priv->regmap),
			     "init regmap fallita\n");

regmap_read(priv->regmap, MYCHIP_REG_ID, &val);
regmap_update_bits(priv->regmap, MYCHIP_REG_CTRL, MYCHIP_CTRL_EN, MYCHIP_CTRL_EN);
```

`volatile_reg` è la parte che si sbaglia più spesso: status, FIFO e registri con clear-on-read devono essere dichiarati volatili, altrimenti la cache restituisce valori vecchi e il driver "non vede" gli eventi.

## Character device

Solo quando nessun sottosistema esistente è adatto.

```c
static const struct file_operations mydev_fops = {
	.owner		= THIS_MODULE,
	.open		= mydev_open,
	.release	= mydev_release,
	.read		= mydev_read,
	.write		= mydev_write,
	.unlocked_ioctl	= mydev_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,	/* userspace 32 bit su kernel 64 bit */
	.llseek		= no_llseek,
};

/* Registrazione con cdev: */
ret = alloc_chrdev_region(&priv->devt, 0, 1, "mydev");
cdev_init(&priv->cdev, &mydev_fops);
priv->cdev.owner = THIS_MODULE;
ret = cdev_add(&priv->cdev, priv->devt, 1);

/* In alternativa, molto più semplice per un singolo device: misc_register(). */
```

ABI ioctl definita correttamente:

```c
#define MYDEV_IOC_MAGIC		'M'
#define MYDEV_IOC_GET_INFO	_IOR(MYDEV_IOC_MAGIC, 1, struct mydev_info)
#define MYDEV_IOC_SET_MODE	_IOW(MYDEV_IOC_MAGIC, 2, __u32)
```

Regole dell'ABI, che è pubblica e per sempre:

- Tipi a larghezza fissa (`__u32`, `__s64`), mai `long` o `int`.
- Padding esplicito, struttura allineata a 8 byte, campi riservati azzerati e verificati.
- I campi riservati vanno controllati come zero in ingresso: è l'unico modo di poterli usare in futuro senza rompere i binari esistenti.
- `compat_ptr_ioctl` se le strutture contengono puntatori.

## DMA

```c
/* Coerente: descrittori e strutture di controllo, allocati una volta. */
priv->desc = dma_alloc_coherent(dev, size, &priv->desc_dma, GFP_KERNEL);

/* Streaming: buffer dati per singola transazione. */
dma_addr = dma_map_single(dev, buf, len, DMA_TO_DEVICE);
if (dma_mapping_error(dev, dma_addr))
	return -ENOMEM;
/* ... il dispositivo lavora sul buffer; il driver NON lo tocca ... */
dma_unmap_single(dev, dma_addr, len, DMA_TO_DEVICE);
```

Regole che, violate, producono corruzione silenziosa:

- Fra `map` e `unmap` il buffer appartiene al dispositivo. Se il driver deve leggerlo prima, serve `dma_sync_single_for_cpu()` e poi `dma_sync_single_for_device()`.
- `dma_mapping_error()` va sempre controllato: con un IOMMU la mappatura può fallire davvero.
- Niente DMA su memoria da stack o da `vmalloc`. Solo `kmalloc`, page allocator, o `dma_alloc_coherent`.
- La maschera DMA si imposta prima di ogni allocazione.
- Sul percorso dei descrittori serve una barriera (`dma_wmb()`) fra la scrittura del contenuto e la scrittura del bit di ownership, altrimenti il dispositivo può vedere un descrittore valido con dati non ancora scritti.

## Sysfs e debugfs

```c
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mydev *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", priv->mode);
}
static DEVICE_ATTR_RW(mode);

static struct attribute *mydev_attrs[] = {
	&dev_attr_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mydev);
/* poi: .dev_groups = mydev_groups nella struct driver */
```

Distinzione da rispettare: **sysfs è ABI stabile** — un attributo, un valore, documentato sotto `Documentation/ABI/`, e non si potrà più cambiare. **debugfs non è ABI**: lì va tutto ciò che è diagnostica, e può cambiare a ogni versione. Mettere diagnostica in sysfs è un errore che ci si porta dietro per anni.

Usa `sysfs_emit()` invece di `sprintf()`: gestisce correttamente il limite di pagina.
