# S5L8702 / iPod classic 6G — hardware

Fuente primaria: `firmware/export/s5l87xx.h` de Rockbox, resuelto con el
preprocesador para `CONFIG_CPU == S5L8702`:

```bash
cpp -dM -P -nostdinc -D__ASSEMBLER__ \
    -DS5L8700=8700 -DS5L8701=8701 -DS5L8702=8702 -DS5L8720=8720 \
    -DCONFIG_CPU=8702 -DMEMORYSIZE=64 s5l87xx.h | sort > s5l8702_macros.h
```

Resultado en `assets/ref/s5l8702_macros.h` (1564 macros).
Todo lo de este documento sale de ahí salvo donde se indique otra cosa.

## Placa

| Componente | Pieza | Fuente |
|---|---|---|
| SoC | Samsung S5L8702, marcado Apple `337S3473 8702` | freemyipod |
| CPU | ARM926EJ-S, 216 MHz (`CPU_FREQ 216000000`) | freemyipod + `config/ipod6g.h` |
| SDRAM | K4X51163PE | freemyipod |
| Flash de utilidad | SST25VF080B (NOR por SPI) | freemyipod |
| Códec de audio | Cirrus Logic **CS42L55** (I²C) | freemyipod + `cscodec-6g.c` |
| PMU | NXP **PCF50635** (I²C) | freemyipod + `pmu-6g.c` |
| Carga USB | LTC4066 | freemyipod |
| Click wheel | controlador **dedicado dentro del SoC** en `0x3C200000`, IRQ 23 | `ipod/button-clickwheel.c` |
| Mic del jack + mando de auriculares | **"Mikey"**, I²C0 dir. `0x72` (no es el click wheel) | `ipod6g/mikey-target.h` |
| Pantalla | 320×240, RGB565 (`LCD_DEPTH 16`), 160 dpi | `config/ipod6g.h` |
| Almacenamiento | **HDD ATA + CE-ATA sobre SDCI** | `ipod6g/storage_ata-6g.c` |

`MODEL_NUMBER 71`, `MODEL_NAME "Apple iPod Classic/6G"`,
`CONFIG_LCD LCD_IPOD6GNANO3G4G`, `HAVE_ATA_DMA`, `ATA_MAX_UDMA 4`.

## Mapa de memoria

### RAM / ROM

| Región | Base | Tamaño |
|---|---|---|
| SDRAM (`DRAM_ORIG`) | `0x08000000` | 64 MB |
| IRAM0 (`IRAM_ORIG`) | `0x22000000` | `0x20000` (128 KB) |
| IRAM1 | `0x22020000` | `0x20000` (128 KB) |
| Bootrom | por confirmar — en el Touch 2G está en `0x0` | — |
| TTB (tablas de páginas, lo pone Rockbox) | `DRAM_ORIG + DRAM_SIZE - 0x4000` | `0x4000` |

> El S5L8720 del Touch 2G tiene IRAM1 de solo `0x10000`; el S5L8702 tiene
> `0x20000`. Total 256 KB de IRAM.

### Periféricos (`IO_BASE 0x38000000`)

| Base | Macro Rockbox | Bloque | ¿Igual en S5L8720? |
|---|---|---|---|
| `0x38000000` | `SHA1_BASE` | Motor SHA-1 | ✅ sí |
| `0x38100000` | `MIU_BASE` | Controlador de memoria | — |
| `0x38200000` | `DMA0_BASE` | DMAC0 (PL080) | ✅ sí |
| `0x38300000` | `LCD_BASE` | Controlador LCD | ❌ (2G: `0x38900000`) |
| `0x38400000` | `DMA_BASE` / `OTGBASE` | USB OTG (Synopsys DWC) | ✅ sí |
| `0x38500000` | `SM1_BASE` | Divisor de reloj SM1 | — |
| `0x38600000` | `USB_HC_BASE` | USB host controller | — |
| `0x38700000` | `ATA_BASE` | **Controlador ATA** | ❌ (2G: DMAC1) |
| `0x38800000` | `USB_FC_BASE` | USB function controller | — |
| `0x38a00000` | `ATA_UNKNOWN_BASE` | Bloque auxiliar ATA / FMI | — |
| `0x38b00000` | `SDCI_BASE` | SD/MMC — usado para **CE-ATA** | ❌ (2G SDIO: `0x38D00000`) |
| `0x38c00000` | `AES_BASE` | Motor AES | ✅ sí |
| `0x38E00000` | `VIC_BASE` | VIC0 (PL192); VIC1 en `+0x1000` | ✅ sí |
| `0x39000000` | `ADM_SFR_BASE` | CalmADM2E (DSP de audio) | — |
| `0x39200000` | `LCDBASE` | Ventanas/framebuffer del LCD | — |
| `0x39400000` | `FMC_BASE` | Flash Memory Controller (NAND) | — |
| `0x39900000` | `DMA1_BASE` | DMAC1 (PL080) | ✅ sí |
| `0x39a00000` | `EIC_BASE` / `SYSALV_BASE` | Controlador de int. externas | — |
| `0x39C00000` | `INT_BASE` | Controlador de interrupciones legacy | — |
| `0x39E00000` | `ECC_BASE` | ECC de NAND | — |
| `0x3C200000` | `WHEEL_BASE` | **Interfaz del click wheel** | ❌ solo classic/nano |
| `0x3C400000` | `USB_PHY_BASE` | PHY USB | ✅ sí |
| `0x3C500000` | `CLK_BASE` | Reloj / PLL | ✅ sí |
| `0x3C600000` | `IIC_BASE` / `MS_BASE` | I²C 0 | ✅ sí |
| `0x3C700000` | `TIMER_BASE` | Timers | ✅ sí |
| `0x3C800000` | `WDT_BASE` | Watchdog | — |
| `0x3CA00000` | `I2S_BASE` | I²S (audio) | — |
| `0x3CB00000` | `SPD_BASE` | S/PDIF | — |
| `0x3CC00000` | `UARTC_BASE_ADDR` | UART0 (consola serie) | ✅ sí |
| `0x3CD00000` | `SPI_BASE` | SPI0 | ❌ (2G: `0x3C300000`) |
| `0x3CE00000` | `ADC_BASE` | ADC | ❌ (2G: SPI1) |
| `0x3CF00000` | `GPIO_BASE` | GPIO | ✅ sí |
| `0x3D100000` | `CHIPID_BASE` | Chip ID | ✅ sí |
| `0x3D200000` | `RTC_BASE` | RTC (y SPI2 según `SPIBASE(2)`) | — |
| `0x3D700000` | `MIU_BASE` (variante) | — | — |
| `0x3F000000` | `ADM_BASE` | CalmADM2E, área de datos | — |

Los tres SPI se seleccionan con `SPIBASE(i)`: `i==2 → 0x3d200000`, y los otros
dos siguen el patrón de `SPI_BASE`.

### UARTs

`UARTC_BASE_ADDR 0x3CC00000`. En S5L8701 el stride es de `0x8000`
(`UARTC1 0x3CC08000`, `UARTC2 0x3CC0C000`); para S5L8702 solo se define el
canal 0 en el header — **pendiente de confirmar** si hay más.

## Números de IRQ del VIC

Extraídos de `s5l8702_macros.h`. Van al par de PL192 en `0x38E00000` /
`0x38E01000` (los IRQ ≥ 32 son del VIC1).

| IRQ | Fuente |
|---|---|
| 0–3 | `IRQ_EXT0..EXT3` |
| 7 | `IRQ_TIMER32` |
| 8 | `IRQ_TIMER` |
| 9, 10, 11 | `IRQ_SPI0/1/2` |
| 14 | `IRQ_LCD` |
| 16, 17 | `IRQ_DMAC0/1` |
| 19 | `IRQ_USB_FUNC` |
| 20 | `IRQ_NAND` |
| 21, 22 | `IRQ_I2C0/1` |
| 23 | **`IRQ_WHEEL`** (click wheel) |
| 24–28 | `IRQ_UART0..UART4` |
| 29 | **`IRQ_ATA`** |
| 31, 32, 33 | `IRQ_EXT4/5/6` |
| 36 | `IRQ_SECBOOT` |
| 39 | `IRQ_AES` |
| 40 | `IRQ_SHA` |
| 43 | `IRQ_NANDECC` |
| 44 | **`IRQ_MMC`** (CE-ATA) |

## Diferencias respecto al S5L8720 (iPod Touch 2G)

Lo que **se mantiene** entre las dos generaciones y por tanto se puede portar
casi tal cual: VIC0/VIC1 (`0x38E00000`), SHA-1 (`0x38000000`), AES
(`0x38c00000`), USB OTG (`0x38400000`), USB PHY (`0x3C400000`), I²C0
(`0x3C600000`), reloj (`0x3C500000`), timers (`0x3C700000`), GPIO
(`0x3CF00000`), chip ID (`0x3D100000`), UART0 (`0x3CC00000`), DMAC0
(`0x38200000`), DMAC1 (`0x39900000`).

Lo que **cambia y hay que rehacer**:

- **CPU**: ARM926EJ-S en vez de ARM1176. Cambia `default_cpu_type` y la
  semántica de los registros cp15 que el modelo del Touch parchea.
- **LCD**: base distinta (`0x38300000` vs `0x38900000`), sin MIPI DSI, panel
  paralelo de 320×240.
- **Almacenamiento**: el Touch usa NAND (FMSS/FMI); el classic usa **disco duro
  ATA** en `0x38700000` más **CE-ATA sobre SDCI** en `0x38b00000`. Nada de esto
  existe en qemu-ios.
- **Entrada**: click wheel en `0x3C200000` (IRQ 23) en vez de multitouch.
- **SPI**: bases distintas.
- **No existen** en el S5L8702: PKE, MIPI DSI, scaler/CSC, TV-out, MBX (GPU).
- **Existe solo aquí**: CalmADM2E (`0x39000000` / `0x3F000000`), I²S
  (`0x3CA00000`), S/PDIF (`0x3CB00000`), ADC (`0x3CE00000`), RTC (`0x3D200000`).

## Pendiente de confirmar

> ⚠️ **Este documento tiene errores corregidos en
> [08-s5l8702-fuentes-externas.md](08-s5l8702-fuentes-externas.md)**, que además
> resuelve casi todo lo de esta sección. Léelo antes de usar la tabla de bases:
> varias entradas de arriba (`SPI0 0x3CD00000`, `ADC 0x3CE00000`, `RTC
> 0x3D200000`, `FMC 0x39400000`, `USB_HC/USB_FC`, `INT_BASE`, `MS_BASE`) son
> bases **legacy del S5L8700/8701** que no aplican al S5L8702.

Resuelto en el doc 08:

- **Bootrom**: `0x20000000`, con alias en `0x0`. Tamaño 50 KiB (wiki) o 64 KiB
  (modelo de freemyipod), sin reconciliar. El remap posterior a IRAM0 existe pero
  no se conoce el registro que lo controla.
- **Volcado público de la bootrom**: no confirmado; hay que volcarla del hierro.
- **UARTs**: 1 controlador, **4 puertos**, stride `0x4000`. Consola en el puerto 0.
- **Bloque `WHEEL`**: registros y secuencia de init documentados (y `mikey-6g.c`
  no tiene nada que ver — es el mando de los auriculares por I²C).
- **NOR SPI**: en SPI0 (`0x3C300000`), 1 MiB, layout SysCfg / NOR boot IM3 /
  ficheros "flsh" documentado.
