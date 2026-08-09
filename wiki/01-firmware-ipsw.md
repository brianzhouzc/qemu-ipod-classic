# Formato del firmware del iPod classic 6G

Material analizado: `iPod_24.1.1.2.ipsw` (60 MB), aportado por el usuario.
Herramienta: `tools/ipod_classic/fwtool.py`.

## Nivel 1 — el `.ipsw`

Es un ZIP con dos entradas:

```
Firmware-24.9.1.2    92.930.048 bytes   <- la partición de firmware
manifest.plist              672 bytes
```

El `manifest.plist` declara:

| Clave | Valor |
|---|---|
| `FirmwareName` | `Firmware-24.9.1.2` |
| `BuildID` | 152207360 (`0x0912_0000`) |
| `VisibleBuildID` | 17989632 (`0x0112_0000`) |
| `FamilyID` | 11 |
| `UpdaterFamilyID` | 24 |

## Nivel 2 — la partición de firmware (`]ih[`)

Es el formato clásico de iPod, el mismo que manipula `ipodpatcher` de Rockbox.

```
0x0000  256 bytes de ASCII-art "STOP / Copyright(C) 2001 Apple Computer, Inc."
0x0100  magic "]ih["
0x0104  uint32  offset del directorio de imágenes  = 0x00004000
0x0108  uint32  0x0003010c  (sin identificar)
...
0x5000  directorio de imágenes
```

**Ojo con el offset del directorio:** el campo dice `0x4000` pero el directorio
real está en `0x5000`. En los S5L870x el área de firmware empieza `0x1000`
dentro del payload, así que `dir_real = 0x1000 + dir_offset`. `fwtool.py` prueba
`0x1000` y luego `0x0`.

### Entradas del directorio (40 bytes cada una, little-endian)

```
0x00  char[4]  dev          (almacenado invertido: 'ATA!')
0x04  char[4]  type         (almacenado invertido: 'rsrc', 'osos', 'aupd', 'hash')
0x08  uint32   id
0x0c  uint32   devOffset    offset dentro del archivo
0x10  uint32   len
0x14  uint32   addr         dirección de carga
0x18  uint32   entryOffset
0x1c  uint32   chksum
0x20  uint32   vers
0x24  uint32   loadAddr
```

### Contenido real de este firmware

| # | type | devOffset | len | addr | vers |
|---|---|---|---|---|---|
| 0 | `rsrc` | `0x00006000` | `0x04e00000` (78 MB) | `0x00000000` | 0 |
| 1 | `osos` | `0x04e07000` | `0x009773e3` (9,5 MB) | **`0x08000000`** | `0x00013000` |
| 2 | `aupd` | `0x05780000` | `0x0011c8b3` (1,1 MB) | `0x08000000` | `0x00013000` |
| 3 | `hash` | `0x0589e000` | `0x00001000` (4 KB) | `0x00000000` | 0 |

- `osos` es el sistema operativo de Apple (Pixo/RetailOS).
- `aupd` es el updater.
- `rsrc` y `hash` están rellenos de `0xFF` en este `.ipsw` — se generan en el
  dispositivo, no vienen aquí.

> **Dato clave:** `addr = 0x08000000` confirma la base de SDRAM del S5L8702, y
> coincide con `DRAM_ORIG` de Rockbox. Ver [02-s5l8702-hardware.md](02-s5l8702-hardware.md).

## Nivel 3 — la cabecera de imagen `8702`

Tanto `osos` como `aupd` empiezan con una cabecera propia del SoC. El layout
siguiente está **verificado al byte** contra ambas imágenes (los campos cuadran
exactamente con el tamaño real de cada blob en el archivo):

```
0x00  char[4]  "8702"          magic (NO invertido)
0x04  char[3]  "1.0"           versión
0x07  uint8    0x03            tipo / cifrado
0x08  uint32   0               sin identificar
0x0c  uint32   tamaño del payload, sin padding
0x10  uint32   footer_offset + footer_size
0x14  uint32   offset del footer, relativo al inicio del payload
0x18  uint32   tamaño del footer
0x40  byte[20] SHA-1
0x54..0x7ff    ceros (relleno)
0x800          empieza el payload
```

Todos los offsets/tamaños de dentro de la cabecera son **relativos al inicio del
payload (0x800)**, no al inicio del archivo. Tamaño total en disco = `0x800 + campo@0x10`.

### Valores medidos

| Imagen | payload (`+0x0c`) | footer (`+0x14`, `+0x18`) | total calculado | último byte no nulo |
|---|---|---|---|---|
| `osos` | `0x976f7c` | `0x977000` + `0xbe3` | `0x9783e3` | `0x9783e3` ✔ |
| `aupd` | `0x11c444` | `0x11c4d0` + `0xbe3` | `0x11d8b3` | `0x11d8b3` ✔ |

El `len` del directorio es siempre `total - 0x1000`. El footer mide `0xbe3` bytes
en ambas — es un blob de firma/certificado de tamaño fijo.

SHA-1 en `+0x40`: `osos` = `8eea8def4f63b9be9e51a8a2d2e4ca1746e6c7f9`,
`aupd` = `063fbf7802b9de9c1b1e163db6af0189426bbd1e`.

## Estado del cifrado

Entropía del payload medida sobre bloques de 64 KB:

| Imagen | primeros 64K | mitad | final |
|---|---|---|---|
| `osos` | 7,937 | 7,997 | 7,998 bits/byte |

Prácticamente 8 bits/byte ⇒ **el payload está cifrado**. No hay cadenas ASCII ni
patrones de código ARM. El cifrado va con la clave GID que vive dentro del motor
AES del SoC.

### Consecuencia para el proyecto

Arrancar el `osos` de Apple en el emulador exige que el modelo del motor AES sepa
descifrar con la clave del S5L8702. Por eso **el primer objetivo no es el OS de
Apple sino Rockbox**, que es open source, se compila sin firmar y se carga desde
`0x08000000` igual que el `osos`. Ver [04-roadmap.md](04-roadmap.md).

### RESUELTO (2026-08-09) — ya no hace falta la clave

El razonamiento de arriba daba por supuesto que había que *tener* la clave GID.
No hace falta: basta con que **descifre el propio aparato**. Con el iPod classic
6G físico corriendo emCORE, su motor AES descifra el payload por USB en 1,2 s.

`osos_decrypted.bin` está en `assets/classic6g/` (gitignorado) y es
inequívocamente el retailOS de Apple: vectores ARM, `Copyright 2001-2008 Apple
Inc.`, la interfaz (`Cover Flow`, `Now Playing`), `RTXC v3.2b`.

El procedimiento completo, el protocolo y la herramienta:
[10-classic6g-emcore.md](10-classic6g-emcore.md).

## Uso de la herramienta

```bash
python tools/ipod_classic/fwtool.py list    "<ruta>/Firmware-24.9.1.2"
python tools/ipod_classic/fwtool.py extract "<ruta>/Firmware-24.9.1.2" <outdir>
```

`extract` genera, por cada imagen: `.img` (blob completo), `.payload` (cuerpo sin
cabecera) y `.footer`.
