# S5L8702 — hallazgos externos y correcciones

Investigación hecha sobre fuentes de terceros (Rockbox `master`, wiki de
freemyipod, `freemyipod/qemu`, `devos50/qemu-ios`). Complementa y **corrige** a
[02-s5l8702-hardware.md](02-s5l8702-hardware.md), que se escribió solo a partir
del preprocesado de `s5l87xx.h`.

---

## 0. Lo más importante: ya existe un modelo QEMU del S5L8702

`github.com/freemyipod/qemu`, rama **`s5l8702-dev`**, tiene un SoC S5L8702
completo *y* una máquina `ipod-classic`. Último commit visto: mayo 2026, en
desarrollo activo. Es un fork de `lemon_jesus/qemu`, que a su vez es fork de
`devos50/qemu-ios` — o sea, la misma base que estamos usando.

```
hw/arm/s5l8702.c                  SoC
hw/arm/ipod-classic.c             máquina (64 MiB, arm926, NOR en SPI0, HDD IDE)
hw/arm/ipod-nano3g.c              máquina hermana (32 MiB, NAND)
include/hw/arm/s5l8702.h          mapa de memoria del SoC
hw/misc/s5l8702-{aes,sha,chipid,clk,clickwheel,jpeg,lcd,miu,
                 nand,nand-ecc,sysic,usbotg,usbphy}.c
hw/gpio/s5l8702-gpio.c   hw/i2c/s5l8702-i2c.c   hw/ide/s5l8702-ata.c
hw/ssi/s5l8702-spi.c     hw/timer/s5l8702-timer.c  hw/watchdog/s5l8702-wdt.c
```

Ya tiene escritos los tres dispositivos que en 03-reuse-qemu-ios.md dábamos por
"hay que hacer de cero": **ATA**, **click wheel** y **LCD de interfaz MPU**.

Invocación de su máquina:

```
-M ipod-classic -cpu arm926 -m 64M \
   -M bootrom=/ruta/s5l8702_bootrom.bin \
   -drive if=mtd,file=nor.bin,format=raw \
   -drive if=ide,file=hdd.img,format=raw
```

Orden de init en `s5l8702_realize()`: CPU → VIC0/VIC1 + glue de flancos → CLK →
AES → SHA → GPIO → SPI0-2 → I2C0-1 → timers → LCD (+ framebuffer en
`0x0FE00000`) → JPEG → 2×PL080 → ATA → click wheel → chipid → NAND + NAND-ECC →
bootrom (+ alias en 0) → IRAM0/IRAM1 → UARTs → MIU → USB OTG → USB PHY → SYSIC.

**Recomendación: partir de ahí en vez de seguir desde cero.**

---

## 1. Correcciones al mapa de memoria

`s5l87xx.h` define muchas bases dentro de `#if CONFIG_CPU==S5L8700 ||
CONFIG_CPU==S5L8701`. Si se preprocesa sin cuidado se cuelan bases que **no**
son del S5L8702.

| Lo que decía 02 | Correcto para S5L8702 | Por qué |
|---|---|---|
| SPI0 = `0x3CD00000` | **SPI0 = `0x3C300000`** | `0x3CD00000` es `SPI_BASE` de S5L8700/01. Para 8702 manda `SPIBASE(i)`: `0→0x3c300000`, `1→0x3ce00000`, `2→0x3d200000`. Confirmado por la wiki de freemyipod y por `S5L8702_SPI*_BASE` del fork de QEMU. |
| `0x3CE00000` = ADC | **SPI1** | `ADC_BASE` es legacy S5L8700; en el classic el ADC está dentro del PMU. |
| `0x3D200000` = RTC | **SPI2** | `RTC_BASE` es legacy. El RTC real del classic está en el PCF50635 (`CONFIG_RTC RTC_NANO2G`, `rtc-6g.c` llama a `pmu_read_rtc`). |
| `0x39400000` = FMC (NAND) | **NAND = `0x38A00000`** | `FMC_BASE 0x39400000` es solo S5L8701. El bloque NAND del 8702 está en `0x38A00000` — es el mismo que Rockbox llama `ATA_UNKNOWN_BASE`. En el S5L8720 el equivalente (FMSS) también está en `0x38A00000`. |
| I²C1 sin listar | **I²C1 = `0x3C900000`** | `IICCON(bus) = IIC_BASE + 0x300000*bus`. Coincide con `I2C1_MEM_BASE 0x3C900000` del Touch 2G. (⚠️ el fork de freemyipod pone `0x3C610000`; parece un error suyo.) |
| `0x38600000` = USB host, `0x38800000` = USB function | **no aplican** | son S5L8700/01. |
| `0x39C00000` = INT_BASE | **no aplica** | el ICU legacy está sustituido por los dos PL192. |
| `0x3C600000` = `MS_BASE` (memory stick) | **no aplica** | colisiona con I²C0; es legacy. |

Bases nuevas que faltaban:

| Base | Bloque | Fuente |
|---|---|---|
| `0x38A02000` | motor ECC de NAND | `S5L8702_NAND_ECC_BASE` |
| `0x38E02000` | bloque de config. de flancos del VIC (`VIC0EDGE0/1`, `VIC1EDGE0/1` en +0x00/04/08/0C) | `s5l87xx.h` |
| `0x39600000` | decodificador **JPEG** | wiki freemyipod |
| `0x3CD00000` | I²S1 (`I2S_BASE + 0x300000`) | `s5l87xx.h` |
| `0x3D400000` | I²S2 (`I2S_BASE + 0xA00000`) | `s5l87xx.h` |

---

## 2. Bootrom y reset — resuelto

- La bootrom vive en **`0x20000000`** y está **además mapeada en `0x00000000`**,
  "porque es donde el core ARM espera los vectores de interrupción"
  (freemyipod.org/wiki/Boot_Process).
- Tamaño: el mapa de memoria del Nano 3G da `0x20000000–0x2000C800` = **50 KiB**.
  El modelo de freemyipod reserva **64 KiB** (`S5L8702_BOOTROM_SIZE 0x10000`).
  No están reconciliados.
- Después del arranque, **IRAM0 pasa a estar mapeada en `0x0`**. Rockbox lo dice
  explícito en `crt0.S`: *"we do not want to execute from 0x0 as iram will be
  mapped there"*, y en `boot.lds`: *"s5l8702/s5l8720 maps address 0 to ROM, IRAM
  or DRAM"*.
- ⚠️ **El registro que controla ese remap no aparece en ninguna fuente.** Ni
  Rockbox ni el modelo de freemyipod lo programan; el modelo simplemente deja el
  alias de la ROM en 0 permanentemente. Podemos hacer lo mismo de momento.
- Los payloads DFU se cargan y ejecutan en **`0x2200030C`** (`mks5lboot
  --mkdfu-raw`).

### ¿Hay volcado público de la bootrom?

**No pude confirmar ninguna descarga pública.**

- La wiki de freemyipod publica el SHA-256
  `9cc05fb8…bfbafb` pero es de una ROM de **Nano 4G (S5L8720)**, no del 8702, y
  no publica el binario.
- Existe de hecho un volcado del 8702 en la comunidad: las dos máquinas del fork
  de freemyipod exigen la propiedad `bootrom=<fichero>` y la escriben en
  `0x20000000`; están desarrollándose activamente contra él.
- Ninguno de los dos repos incluye el binario en el árbol (lo comprobé).
- La vía documentada es volcarla uno mismo: *"To dump the firmware of any iPod
  classic or iPod nano from version 3 on you need to run own code on the device"*
  (wiki, `Dumping_firmware`). `freemyipod/wInd3x` tiene `cmd_dump.go` y un camino
  para Nano 3G (mismo SoC), pero su tabla de dispositivos **no incluye el iPod
  classic**.

**Implicación de diseño:** hacer la bootrom una propiedad *opcional* de la
máquina y ofrecer un camino alternativo que cargue un cuerpo IM3 ya descifrado
directamente en `IRAM1 + IM3HDR_SZ` y ponga el PC ahí — que es exactamente lo que
ve la segunda etapa real (`boot.lds`: `MOVE_AREA : ORIGIN = IRAM1_ORIG + IM3HDR_SZ`).

---

## 3. Cadena de arranque

```
Power on
 └─ BootROM @0x20000000 (alias en 0x0), ~50 KiB, mask ROM
     ├─ stacks/modos, arranca PLLs, abre clock gates de AES/NAND/NOR/USB
     ├─ mira straps GPIO / botones (los pone el controlador del click wheel)
     └─ elige: (a) 2ª etapa desde NOR  ← ESTE es el camino del classic 6G
               (b) 2ª etapa desde NAND ← nano 3G
               (c) DFU por USB         ← fail-safe / SELECT+MENU ~12 s
     └─ verifica la imagen IMG1/IM3, la descifra y salta a su entrypoint
 └─ 2ª etapa: "bootloader" (desde NOR/NAND) o "WTF" (por DFU)
     ├─ basada en EFI, modular; levanta DRAM, LCD, UART, los VIC, (FTL en nano)
     └─ carga y verifica el siguiente IMG1
 └─ OSOS — el RetailOS de Apple, leído del **disco duro**
```

### Layout de la NOR del classic 6G (`norboot-target.h`)

```
 1 MiB        ┌──────────────┐
              │  dir "flsh"  │  (últimos 0x200 bytes)
 1MiB-0x200   ├──────────────┤
              │ Ficheros 1..N│  sistema de ficheros "flsh"
              ├──────────────┤
              │   Sin usar   │
 160 KiB      ├──────────────┤
              │  NOR Boot    │  imagen IM3, máx 128 KiB
              │  original    │    0x000–0x800 : cabecera IM3
  32 KiB      ├──────────────┤    0x800–...   : volumen EFI
              │    SysCfg    │
      0       └──────────────┘

NOR_SZ 0x100000   NORBOOT_OFF 0x8000   NORBOOT_MAXSZ 0x20000   SPI_PORT 0
```

`SysCfg` es un blob con magic `'SCfg'` y 9 entradas en el classic 6G: `SrNm`,
`FwId`, `HwId`, `HwVr`, `Codc`, `SwVr`, `MLBN`, `Mod#`, `Regn`. Habrá que
sintetizar uno plausible.

El bootloader dual de Rockbox se instala **en la NOR** en `NORBOOT_OFF`; Rockbox
en sí se carga desde el **disco duro FAT32**.

### Formato IMG1 / IM3

```c
struct Im3Info {
  u8  ident[4];      /* "8702" */
  u8  version[3];    /* "1.0"  (el 8720 usa "2.0") */
  u8  enc_type;      /* 1 SIGNED_ENCRYPTED, 2 SIGNED,
                        3 X509_SIGNED_ENCRYPTED, 4 X509_SIGNED */
  u32 entry;         /* LE */
  u32 data_sz;       /* LE */
  union { enc12 { u8 data_sign[16]; u8 rsvd[32]; }
          enc34 { u32 sign_off; u32 cert_off; u32 cert_sz; u8 rsvd[36]; } } u;
  u8  info_sign[16]; /* AES(GID, SHA1(hdr[0:0x40])[0:16]) */
} __packed;

IM3HDR_SZ = 0x800   /* el S5L8720 usa 0x600 */
```

Verificación: SHA-1 de los primeros `0x40` bytes, cifrado con la **clave GID**,
comparado con `info_sign`; luego cadena X.509 sobre el cuerpo, con raíz fijada en
la ROM (huella SHA-1 `61:1e:5b:66:…:6c:60`, Apple Root CA) y serial de la hoja
empezando por `01:fb:01:fb` o `01:fb:00:fb`. Las imágenes de iPod usan la clave
GID para **todo** el AES (nada de "key 0x837"), y los formatos 1/2 descifran
usando la dirección de *cifrado* del motor. Las imágenes 1.0 enviadas por DFU
llevan un CRC32 al final.

Claves del motor AES (`crypto-s5l8702.h`): `HWKEYAES_GKEY = 1` (clave de modelo /
GID), `HWKEYAES_UKEY = 2` (clave única del dispositivo / UID). Dirección:
`AESKEYLEN = 9` cifra, `8` descifra. Hay que escribir `AESTYPE2 = ~AESTYPE`.
Es un motor **bus-master**: `AESINADDR`/`AESOUTADDR`/`AESAUXADDR` son punteros
físicos y Rockbox hace flush de dcache antes de `AESGO = 1`.

---

## 4. Click wheel — corrección importante

02-s5l8702-hardware.md decía *"Click wheel: controlador Mikey (CD3272-like), por
SPI"*. **Eso es incorrecto**, mezcla dos cosas distintas:

- **"Mikey"** es el controlador del **micrófono del jack y del mando de los
  auriculares**, y va por **I²C0 en la dirección `0x72`**. No existe en los
  modelos de 2007. (`ipod6g/mikey-target.h`: *"Mikey is the internal controller
  for the headphone jack microphone and inline remote (I2C bus 0, address
  0x72)"*.)
- El **click wheel** tiene un **controlador dedicado dentro del SoC** en
  `0x3C200000` (`WHEEL_BASE`), clock gate 33, **IRQ 23**. Ni I²C ni SPI.

Registros y secuencia de init (`firmware/target/arm/ipod/button-clickwheel.c`):

```
0x00 WHEEL00   0x04 WHEEL04   0x08 WHEEL08   0x0C WHEEL0C
0x10 WHEEL10   0x14 WHEELINT  0x18 WHEELRX   0x1C WHEELTX
```

```c
clockgate_enable(CLOCKGATE_CWHEEL, true);
PCON14 = (PCON14 & ~0x00ffff00) | 0x00222200;  /* pines 2..5 del puerto 14 */
WHEEL00 = 0;            /* parar */
WHEELINT = 7;           /* limpiar las 3 fuentes de int */
WHEEL10 = 1;
WHEEL00 = 0x380000;
WHEEL08 = 0x20000;
WHEELTX = 0x8000023A;   /* comando al MCU de la rueda */
WHEEL04 |= 1;           /* arrancar */
```

ISR: leer `WHEELINT`, escribir de vuelta los bits 4/2/1 por separado para
limpiarlos, luego leer `WHEELRX`.

Formato del paquete en `WHEELRX`: si `(status & 0x8000FFFF) == 0x8000023A`
entonces bit 16 = SELECT, 17 = RIGHT, 18 = LEFT, 19 = PLAY, 20 = MENU. La
posición absoluta de la rueda son **96 clicks por vuelta**
(`WHEELCLICKS_PER_ROTATION 96`).

Señales relacionadas:
- **Hold switch**: se lee del **PMU** (`pmu_holdswitch_locked()`), GPIO2 del PCF50635.
- **Auriculares insertados**: `PDAT(10) & (1<<6)`.
- LDO de la rueda: `LDO_CWHEEL = 6` del PMU.

---

## 5. UARTs — resuelto

Un solo controlador en `0x3CC00000` con **4 puertos**, stride **`0x4000`**:

| Puerto | Base | IRQ |
|---|---|---|
| 0 | `0x3CC00000` | 24 |
| 1 | `0x3CC04000` | 25 |
| 2 | `0x3CC08000` | 26 |
| 3 | `0x3CC0C000` | 27 |

(`UARTC_N_PORTS 4`, `UARTC_PORT_OFFSET 0x4000`. El IRQ 28 / puerto 4 está
marcado como *"obsolete/not implemented on s5l8702"*, aunque el fork de
freemyipod instancia igualmente uno en `0x3CC10000`.)

Layout de registros por puerto (estilo Samsung S3C, `firmware/export/uc87xx.h`),
FIFO de 16 bytes:

```
0x00 ULCON   0x04 UCON    0x08 UFCON   0x0C UMCON
0x10 UTRSTAT 0x14 UERSTAT 0x18 UFSTAT  0x1C UMSTAT
0x20 UTXH    0x24 URXH    0x28 UBRDIV
0x2C UABRCNT 0x30 UABRSTAT   (autobaud)
0x34 UBRCONTX 0x38 UBRCONRX  (fine-tune)
```

**La consola serie es el puerto 0**, con reloj **ECLK = 12 MHz**
(`UCON_CLKSEL_ECLK`). En el classic 6G esa línea sale por el **conector dock** y
por defecto lleva el protocolo de accesorios de Apple (iAP); Rockbox la
reutiliza para `logf` si se compila con `LOGF_SERIAL`. Mux de pines:
`PCON0 = (PCON0 & 0xff00ffff) | 0x00220000`. Para 115200 con 12 MHz:
`UBRDIV = 6 | (0xffffff << 8)`. El mux de los puertos 2 y 3 es desconocido.

---

## 6. Almacenamiento — el punto clave

El iPod classic 6G **arranca y guarda el SO en un disco duro**, no en NAND.
`CONFIG_STORAGE STORAGE_ATA`, `HAVE_DISK_STORAGE`, `HAVE_ATA_DMA`,
`ATA_MAX_UDMA 4`, `HAVE_LBA48`, `HAVE_ATA_SMART`. No hay driver de NAND: el del
nano 3G es un stub (`nand_init() { /* TODO */ return 0; }`). La placa del
classic no lleva chip NAND (freemyipod solo lista SDRAM, la NOR SPI, el códec,
el PMU y el LTC4066). **No hace falta FTL.**

Hay **dos** conexiones posibles, elegidas en runtime por un strap:

```c
ceata = PDAT(11) & BIT(1);   /* storage_ata-6g.c:1152 */
```

- `ceata == 0` → **ATA paralelo** por el controlador de `0x38700000`
  (clock gate 5, IRQ 29). Discos ZIF PATA de 1.8".
- `ceata == 1` → **CE-ATA**: taskfiles ATA encapsulados en comandos MMC a través
  del **SDCI de `0x38B00000`** (clock gate 9, IRQ 44, `CEATA_MMC_RCA 1`).

Para el modelo basta implementar el camino PATA y dejar `PDAT(11) bit 1` a 0.

⚠️ Los registros del taskfile del controlador ATA son **de 32 bits, uno por
palabra MMIO** — no es un bloque IDE legacy de 8 bits, así que el core `ide`
genérico de QEMU no vale tal cual. Además `ATA_PIO_READY & 2` bloquea las
lecturas y hace falta una lectura dummy previa para que el valor se enclave
(`ata_read_cbr()`).

---

## 7. Otras cosas que faltaban

**Relojes** (`CLK_BASE 0x3C500000`): osciladores del classic 6G — `OSC0 = 12 MHz`
(cristal), `OSC1 = 32768 Hz` (lo da el PMU en el 6G; cristal en el nano 3G),
`UNKOSC = 24 MHz`, `ECLK = 12 MHz` (referencia de los timers). Plan de reloj de
Rockbox: `{cdiv 1, hdiv 2, hprat 2, hsdiv 4}` → CClk 216 / HClk 108 / PClk 54 /
SM1Clk 27 MHz. Reboot por software: escribir `0x100000` en `WDT_BASE`.

**Timers** (`0x3C700000`): A–D de 16 bits (C y D parecen no implementados),
E–H de 32 bits en `0xA0/0xC0/0xE0/0x100` con estado compartido en **`TSTAT`
(`0x118`)**, y un timer I de 64 bits a 5 µs en `0x80/0x84`. `USEC_TIMER` = `TECNT`
con `TEPRE = 11` y `TECON` bit 6 (ECLK).

**EIC** (`0x39A00000`): 7 grupos × 32. `EIC_INTLEVEL(g) = 0x80+4g`,
`INTSTAT = 0xA0+4g`, `INTEN = 0xC0+4g`, `INTTYPE = 0xE0+4g`. Los pines GPIO
mapean a los grupos **6..3 en orden inverso**: `EIC_GROUP(n) = 6 - (n>>5)`,
`EIC_INDEX(n) = (0x18 - (n & 0x18)) | (n & 7)`. Grupo 0: solo bit 0 (fin del
timer "alive") y bit 2 (cable USB). Los grupos van a IRQ **0,1,2,3,31,32,33**.
⚠️ el fork de freemyipod los pone en IRQ 9..15 — Rockbox manda, que es lo que
corre en el hierro.

**GPIO** (`0x3CF00000`): 16 grupos × 8 pines = 128. Stride de grupo **`0x20`**
(no `0x10`, eso es el 8700/01). `PCON/PDAT/PUNA/PUNB/PUNC` en +0/4/8/C/10,
`GPIOCMD` en **`+0x200`**, `GPIOUNK380` en `+0x380`.

**DMA**: dos PL080. Líneas de petición útiles — DMAC0: `0x3` LCD_WR, `0xA/0xB`
I2S0 TX/RX, `0x4/0x5` SPI0, `0x6/0x7` UART0. DMAC1: `0x0/0x1` CE-ATA WR/RD.
Direcciones de datos: LCD = `LCD_BASE + 0x40`, UARTn = `base + n*0x4000 + 0x20/0x24`.

**LCD** (`0x38300000`): **no es un framebuffer con scanout**, es una interfaz
MPU/8080 contra un panel con su propia GRAM. Rockbox mantiene un shadow buffer en
DRAM y lo empuja por DMAC0 (petición 3) a `LCD_WDATA` (`+0x40`). Hay **cuatro
paneles distintos** según revisión, y el tipo se lee de **GPIO puerto 6 bits
[5:4]**: `lcd_info_list[(PDAT6 & 0x30) >> 4]`. Tipos 0 y 1 usan juego de
comandos de 8 bits (tipo ILI9340), tipos 2 y 3 de 16 bits (tipo ILI9320/ILI9326).
Todos `PAR18`. El backlight **no** es del SoC: va por el PMU (`pmu_write(0x28,
brillo)`, `pmu_write(0x29, on/off)`).

**I²C0 — dispositivos del classic 6G** (direcciones de 8 bits como las usa
Rockbox, entre corchetes las de 7 bits):

| Dir | Dispositivo |
|---|---|
| `0xE6` [`0x73`] | PMU NXP PCF50635 |
| `0x94` [`0x4A`] | códec Cirrus Logic CS42L55 |
| `0x72` [`0x39`] | "Mikey" (mic del jack + mando de auriculares) |

**Audio**: I²S0 en `0x3CA00000` para reproducción y grabación. Reset del códec en
**GPIO puerto 3 bit 3** (`PDAT(3) &= ~8`), MCLK con `CLKCON3` bit 15.
`I2STXCON = 0x0B100019`, `I2SCLKCON = 1`, `I2STXCOM = 0xE` start / `0xA` stop.
**No existe modelo de I²S en ninguno de los dos forks de QEMU** — habría que
escribirlo.

**USB**: DWC OTG en `0x38400000`, **9 endpoints** (`0b…111101010000000111101011`),
6 usados, FIFOs dedicadas (el 8701 sí necesita `USB_DW_SHARED_FIFO`). PHY en
`0x3C400000` con `OPHYPWR 0x00`, `OPHYCLK 0x04`, `ORSTCON 0x08`, `OPHYUNK3 0x18`,
`OPHYUNK1 0x1C`, `OPHYUNK2 0x44`. Detección de cable por **EIC grupo 0 bit 2 →
IRQ 33**.

---

## 8. Sigue sin saberse

1. El **registro de remap** de `0x0` (ROM → IRAM0/DRAM).
2. Tamaño exacto de la bootrom (50 KiB vs 64 KiB).
3. Un volcado público y descargable de la bootrom del S5L8702.
4. Los **números de parte de los cuatro paneles LCD** del classic 6G.
5. Mux de pines de UART2 y UART3.
6. Semántica de los registros `WHEEL00/04/08/0C/10` más allá de los valores mágicos.
7. Qué registro exacto toca el firmware en `0x38A00000` durante el power-up del disco.
8. Si el S5L8702 tiene un bloque PKE en `0x3D000000` (el 8720 sí; el clock gate
   45 se describe como *"related to 0x3d00_0000 (secure boot ???)"*).

## Fuentes nuevas

- `raw.githubusercontent.com/Rockbox/rockbox/master/firmware/export/s5l87xx.h`
  (⚠️ **no** existe `firmware/target/arm/s5l8702/s5l8702.h`)
- `firmware/export/uc87xx.h`, `firmware/target/arm/ipod/button-clickwheel.c`,
  `firmware/target/arm/s5l8702/norboot-target.h`, `utils/mks5lboot/README`
- `github.com/freemyipod/qemu` rama `s5l8702-dev`
- `github.com/freemyipod/defs` (`memmap.py`, `clockgates.py`)
- `github.com/freemyipod/wInd3x` (`pkg/devices/devices.go`)
- freemyipod.org/wiki/: `Boot_Process`, `Bootrom`, `IMG1`, `Nano_3G/Memory_Map`,
  `Classic_6G`, `Dumping_firmware`
