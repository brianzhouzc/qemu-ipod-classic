# Qué se reaprovecha de qemu-ios

> **Documento de planificación, histórico (10 de agosto de 2026).** Se escribió
> antes de modelar nada. La máquina `-M iPod-Classic` ya arranca el retailOS de
> Apple, y conviene decir **qué se reutilizó de verdad**, porque no se parece a
> lo que hay debajo:
>
> - **Se reutilizó tal cual, y sin tocar una línea**: los dos VIC (`hw/intc/pl192.c`),
>   los dos DMAC (`hw/dma/pl080.c`) y la UART (`hw/char/exynos4210_uart.c`).
>   Tres aciertos de esta tabla, y el de los PL080 resultó ser **la pieza que
>   destrabó el arranque con disco**.
> - **Se intentó reutilizar y salió mal**: `ipod_touch_clock.c`. Coincide con el
>   S5L8702 hasta `0x4c` **por casualidad**, y contestaba a `SWRCON`, `RSTSR` y
>   `PLLMOD2` con `hw_error()`, que no es un log sino un `abort()` del proceso.
>   Se reescribió (`e7f8bfaf3`).
> - **Todo lo demás se escribió de cero**: I²C y PMU, ATA con DMA, panel LCD,
>   GPIO, timers, click wheel y el compositor de vídeo. No por gusto, sino
>   porque el S5L8702 los tiene distintos y porque **tres de esos bloques no
>   están en ninguna fuente externa** — el compositor de `0x38900000`, el
>   generador de `0x3C100000` y los registros del motor de trama del LCD sólo
>   existen en el código de Apple.
>
> La lección, que es la del bit de estado que nunca llega: **un modelo prestado
> que decodifica *casi* el mismo mapa es peor que no tener modelo**, porque
> falla en las direcciones que el mapa nuevo añadió y lo hace de la forma más
> ruidosa posible.

> **Aviso: ya existe un modelo del S5L8702 en QEMU.**
>
> `github.com/freemyipod/qemu`, rama **`s5l8702-dev`**, contiene un modelo
> completo (WIP) del SoC S5L8702 y una máquina `ipod-classic` — 64 MiB, arm926,
> NOR por SPI0, disco IDE, PCF5063x en I2C0. Últimos commits de mayo de 2026.
> Ficheros: `hw/arm/s5l8702.c`, `hw/arm/ipod-classic.c`,
> `include/hw/arm/s5l8702.h`, más ~19 modelos de dispositivo bajo
> `hw/{misc,gpio,i2c,ide,ssi,timer,watchdog}/s5l8702-*.c`.
>
> Ese fork desciende del mismo `devos50/qemu-ios` del que partimos aquí. **Si
> se retoma el Classic 6G, hay que partir de ahí en vez de desde cero**, y usar
> `devos50/qemu-ios@ipod_touch_2g` solo como contraste de direcciones base.
>
> Lo descubrimos tarde: este documento se escribió antes de saberlo.



La rama `ipod_touch_2g` modela el S5L8720. El S5L8702 es de la misma familia, así
que buena parte de los modelos de dispositivo sirven.

**Hecho clave que lo hace fácil:** ningún modelo de dispositivo de qemu-ios
codifica su dirección base. Todos hacen `memory_region_init_io(...)` en offset 0
y es la máquina quien los coloca con `memory_region_add_subregion(sysmem,
XXX_MEM_BASE, ...)`. Reubicar el mapa es editar la tabla de `*_MEM_BASE` de
`include/hw/arm/ipod_classic.h`.

## Reutilizable casi tal cual

| Modelo | Archivo | Notas para el S5L8702 |
|---|---|---|
| VIC PL192 ×2 | `hw/intc/pl192.c` (stock) | Misma base `0x38E00000`/`0x38E01000`. Cero cambios. |
| DMAC PL080 ×2 | `hw/dma/pl080.c` (stock) | Bases `0x38200000` y `0x39900000`. |
| UART | `hw/char/exynos4210_uart.c` (stock) | Ya se usa para 1G y 2G. Base `0x3CC00000`. |
| Reloj / PLL | `ipod_touch_clock.c` | Fichero de registros puro, sin lógica. Base `0x3C500000`. |
| GPIO | `ipod_touch_gpio.c` | Hay que remapear los pines de botones. |
| SYSIC | `ipod_touch_sysic.c` | Power gating + controlador de int. de GPIO. |
| I²C | `hw/i2c/ipod_touch_i2c.c` | Samsung clásico. Base `0x3C600000`, idéntica. |
| SPI | `ipod_touch_spi.c` | Layout de registros común a toda la familia. **Cuidado:** selecciona el periférico esclavo con un global estático `base_addr` vía `set_spi_base(N)`; conviene convertirlo en propiedad qdev. |
| Chip ID | `ipod_touch_chipid.c` | Cambiar `0x8720` por `0x8702` en offset `0x8`. |
| SHA-1 | `ipod_touch_sha1.c` | Respaldado por OpenSSL. Misma base `0x38000000`. |
| USB PHY | `ipod_touch_usb_phys.c` | Misma base `0x3C400000`. |
| USB OTG | `ipod_touch_usb_otg.c` | Modelo device-mode de Synopsys DWC. Solo hay que dar los cuatro valores `GHWCFG` del S5L8702. |
| NOR SPI | `ipod_touch_nor_spi.c` | El classic también lleva NOR por SPI (SST25VF080B). |
| PMU | `ipod_touch_pcf50633_pmu.c` | El classic usa **PCF50635** — misma familia, revisar registros contra `ipod6g/pmu-6g.c`. |
| Códec audio | `ipod_touch_cs42l58.c` | El classic usa **CS42L55** — revisar contra `ipod6g/cscodec-6g.c`. |
| "Mikey" | `ipod_touch_cd3272_mikey.c` | El classic tiene el mismo IC de accesorios/wheel. Ver `ipod6g/mikey-6g.c`. |

## Hay que reescribir el decode

| Modelo | Por qué |
|---|---|
| Timer (`ipod_touch_timer.c`) | El modelo del 2G es muy tosco: solo emula "Timer 4" en offset `0xA0` más el contador de 64 bits. El S5L8702 tiene 4 timers de 16 bits + 4 de 32 bits con otro layout. Se aprovecha el esqueleto. |
| LCD (`ipod_touch_lcd.c`) | Base distinta y controlador distinto (panel paralelo, no MIPI). El esqueleto de scanout a 10 Hz sí vale. |
| AES (`ipod_touch_aes.c`) | El modelo del 2G **no emula la clave GID**: reparte cinco claves/IV precalculadas en orden de llamada y además parchea direcciones físicas concretas de iBoot. Todo eso es específico del firmware del Touch 2G y hay que quitarlo. |

## Hay que escribir de cero

| Bloque | Base | Comentario |
|---|---|---|
| **ATA** | `0x38700000` (IRQ 29) | El hueco más grande. QEMU tiene `hw/ide/` como base, pero el controlador del S5L8702 hay que modelarlo mirando `ipod6g/storage_ata-6g.c`. |
| **CE-ATA / SDCI** | `0x38b00000` (IRQ 44) | Ruta alternativa de almacenamiento. `ipod6g/mmcdefs-target.h` documenta los comandos. |
| **Click wheel** | `0x3C200000` (IRQ 23) | No hay nada equivalente en qemu-ios (el Touch usa multitouch por SPI). |
| **I²S** | `0x3CA00000` | Audio. |
| **ADC** | `0x3CE00000` | |
| **RTC** | `0x3D200000` | |

## No aplica al S5L8702

PKE (RSA), MIPI DSI, scaler/CSC, TV-out, MBX (GPU), SDIO/BCM4325 (WiFi), FMSS.
El modelo de NAND que sí serviría, si hiciera falta, es el de la rama
`ipod_touch_1g` (`ipod_touch_nand.c`, registros FMI: `FMCTRL0`, `FMCTRL1`,
`CMD`, `FMADDR0/1`, `FMANUM`, `FMDNUM`, `FMCSTAT`, `FMFIFO`, `RSCTRL`), que
coincide con el `FMC_BASE` del S5L8702.

## Cómo arranca la máquina del Touch 2G (para copiar el patrón)

- `mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm1176")` → nosotros: `arm926`.
- No usa `machine->ram`; todas las regiones se crean con
  `memory_region_init_ram()` desde un helper `allocate_ram()`.
- La bootrom se carga en la física `0x0` (región de 128 KB) desde la propiedad
  de máquina `bootrom=`; si el archivo no existe, `exit(1)`.
- `ipod_touch_cpu_reset()` hace `cpu_set_pc(cpu, VROM_MEM_BASE)` → arranca en `0x0`.
- **No hay ruta `-kernel`** ni `arm_load_kernel()`. Para nuestro primer hito nos
  interesa añadir precisamente eso: cargar un binario en `0x08000000` y saltar
  ahí, saltándonos la bootrom.
