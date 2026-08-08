# Roadmap

## Dos objetivos, dos máquinas

El proyecto arrancó apuntando al **iPod classic 6G** (S5L8702) y se topó con que
su firmware está cifrado con una clave que vive en el silicio. Al aparecer un
**iPod Video 5G** físico, cuyo firmware está en claro, el objetivo de arrancar
el sistema original de Apple se movió allí.

| Máquina | SoC | Estado |
|---|---|---|
| `iPod-Classic` | S5L8702 | ejecuta código, timers e interrupciones verificados |
| `iPod-Video` | PP5022C | **el bootloader de Rockbox arranca y dibuja en pantalla** |

El trabajo del Classic sigue intacto y es reaprovechable si algún día
conseguimos un `osos` descifrado.

## iPod Video (PP502x) — estado

Con `-M iPod-Video,entry=0x735a00 -kernel osos_full.bin`, el bootloader de
Rockbox extraído del aparato real recorre esta secuencia:

1. Lee `PROCESSOR_ID` (`0x60000000`) y se reconoce como CPU
2. Espera a que el COP declare que duerme (`COP_CTL`, bit 31)
3. Se copia a sí mismo a IRAM (`0x40000000`)
4. Configura GPIO — seis escrituras de dirección de pines
5. Consulta la revisión de hardware en `0x405c`
6. Hace **transacciones I²C** contra el PMU y el códec (`0x7000c100`/`0x104`/`0x120`)
7. Llega al **controlador IDE** (`+0x1fc`), buscando `rockbox.ipod` en la FAT32

Modelado hasta ahora: DRAM, IRAM, ventana de flash NOR con la revisión de
hardware, `PROCESSOR_ID`, `CPU_CTL`/`COP_CTL`, `USEC_TIMER`, UART 16550,
controlador de interrupciones, IDE, click wheel, GPIO, caché, remap, BCM/LCD,
reloj/PLL y control de dispositivo.

### El bootloader ya pinta en la pantalla emulada

Estado actual: el bootloader de Rockbox 4.0 arranca entero y escribe en el LCD:

```
No partition
found (0).
Insert USB cable
and fix it.
4.0
```

Para llegar hasta ahí hubo que quitar cuatro bloqueos, todos del mismo tipo —
un bit de estado que el hardware real pone y nosotros devolvíamos a cero, con
el firmware esperándolo en un bucle sin salida:

| Síntoma | Causa | Arreglo |
|---|---|---|
| bucle leyendo `0x6000603c` | `PLL_STATUS` bit 31 = PLL enganchado. `system-pp502x.c`: `while (!(PLL_STATUS & 0x80000000));` | bloque de reloj en `0x60006000`, con el bit 31 siempre puesto |
| `Invalid write at 0xF0004000` | `cache_invalidate_special()` escribe a mano todo el array de estado de la caché, que no estaba mapeado | RAM en `0xf0000000`–`0xf000efff` |
| 20 M lecturas de `0x70000028` | bit 7 = el bloque USB salió de reset. `usb-fw-pp502x.c`: `while ((inl(0x70000028) & 0x80) == 0);`. Ese registro **no aparece en `pp5020.h`** | bloque `DEV_CTL` en `0x70000000`, con el bit 7 siempre puesto |
| todo corría pero la pantalla seguía negra | `GPO32_VAL` bit 14 alimenta al Broadcom. Si está a 0, `lcd-video.c` cree que debe inicializar el BCM subiendo su firmware `vmcs` desde la NOR (que no tenemos), deja `display_on = false`, y **`lcd_update_rect()` descarta todos los dibujados sin decir nada** | `GPO32_VAL` arranca con `0x4000`, reproduciendo lo que deja el cargador de Apple |

La lección: en este SoC casi todo fallo se manifiesta igual — el emulador
parece vivo, no hay ninguna excepción, y el firmware está esperando un bit.
`-d unimp` con `sort | uniq -c | sort -rn` encuentra el bucle en segundos.

Ojo con `-d unimp`: cuando el guest entra en un bucle cerrado sobre un registro
sin modelar, el log llega a decenas de millones de líneas y frena la emulación
tanto que parece colgada. Conviene medir siempre sin `-d` antes de concluir que
algo no avanza.

### Siguiente: `No partition found`

El bootloader llega a `disk_mount_all()` y no monta nada. Lo que ya está
descartado:

- **El ATA funciona.** El bootloader habría dado `EATA` antes de llegar aquí;
  `storage_init()` pasó, así que el IDENTIFY responde.
- **La tabla de particiones de la imagen es correcta**: firma `55AA`, entrada 1
  de tipo `0x0b`, LBA 160650, 524288 sectores, y el total cuadra con el tamaño
  del fichero.
- **El FAT32 es válido**: 512 B/sector, 2 FATs, `FATsz32 = 4033`, root en el
  clúster 2, `dataclusters ≈ 516190` (muy por encima del mínimo de 65525 que
  exige `fat_mount_internal`), firma `55AA` en su VBR.
- **`BPB_HiddSec = 0`** aunque la partición empieza en 160650, pero Rockbox no
  mira ese campo (no aparece en `firmware/common/fat.c`).

Queda por comprobar si el guest llega a pedir el sector 160650 y qué devuelve
esa lectura. Es decir: instrumentar el camino de lectura del IDE, no seguir
mirando la imagen.

## Estrategia (iPod classic 6G)

El `osos` de Apple está cifrado con la clave GID del SoC
(ver [01-firmware-ipsw.md](01-firmware-ipsw.md)), así que no es un objetivo
inicial razonable. **El primer objetivo es arrancar Rockbox**, que es open
source, se compila sin firmar, tiene drivers a nivel de registro para todo el
S5L8702 y se carga en `0x08000000` — exactamente igual que el `osos`.

Ventaja de rebote: los drivers de Rockbox son la especificación ejecutable
contra la que validar cada periférico que modelemos.

## Hitos

### H0 — Infraestructura ✅
- [x] Clonar `devos50/qemu-ios`, crear rama `ipod_classic_6g`
- [x] Compilar el baseline (`arm-softmmu`) en WSL
- [x] Herramienta de parseo del firmware (`tools/ipod_classic/fwtool.py`)
- [x] Extraer `osos` / `aupd` del `.ipsw`
- [x] Mapa de memoria y de IRQs del S5L8702 documentado

### H1 — Máquina mínima que ejecuta instrucciones ✅
- [x] `include/hw/arm/ipod_classic.h` con la tabla de bases del S5L8702
- [x] `hw/arm/ipod_classic.c`: CPU ARM926EJ-S, SDRAM `0x08000000` (64 MB),
      IRAM `0x22000000` (256 KB)
- [x] Entradas en `hw/arm/meson.build`, `hw/arm/Kconfig` y
      `configs/devices/arm-softmmu/default.mak` (`CONFIG_IPOD_CLASSIC_6G`)
- [x] Ruta de arranque tipo `-kernel`: cargar un binario en `0x08000000` y
      poner ahí el PC (saltarse la bootrom, que no tenemos)
- [x] Los 32 periféricos aún no modelados registrados como
      `create_unimplemented_device()`, para que `-d unimp` diga qué toca el guest
- [x] Comprobar con un `.bin` ARM trivial (`tools/ipod_classic/mktestbin.py`)

**Resultado:** `-M iPod-Classic` aparece en `-M help` y el binario de prueba
escribe `S5L8702 alive` por la consola serie. Eso valida de golpe: PC inicial en
`0x08000000`, SDRAM mapeada, y UART0 en la dirección correcta.

```
$ ./qemu-system-arm -M iPod-Classic -kernel hello.bin \
      -display none -monitor none -serial stdio -d unimp
S5L8702 alive
```

### H2 — Consola serie e interrupciones ✅
- [x] VIC0/VIC1 (PL192) en `0x38E00000` / `0x38E01000`
- [x] Reloj/PLL en `0x3C500000` (modelo reutilizado del iPod Touch)
- [x] UART0 en `0x3CC00000` (exynos4210_uart, IRQ 24)
- [x] Timers en `0x3C700000` — bloque completo escrito de cero
      (`hw/arm/ipod_classic/ipod_classic_timer.c`)
- [x] Alias de IRAM0 en la dirección `0x0`, que es de donde el ARM926 coge las
      excepciones
- [x] Ruta de interrupción verificada de extremo a extremo

**Resultado:** el test `tools/ipod_classic/tests/timer_irq.c` configura el timer
B a 10 kHz exactamente como Rockbox, instala una tabla de vectores en `0x0`,
desenmascara la IRQ 8 en VIC0 y cuenta interrupciones:

```
S5L8702 timer test
counter runs: yes 0x00000001 0x00000002
waiting for interrupts...
got 0x00000005 timer interrupts
TIMER IRQ OK
```

El log de `-d unimp,guest_errors` sale vacío: ningún acceso a periférico no
modelado, ninguna dirección fuera de sitio.

#### El bloque de timers

Reconstruido a partir de `timer-s5l8702.c`, `kernel-s5l8702.c` y el comentario
sobre `TSTAT` en `s5l87xx.h`:

- 8 canales. **A–D de 16 bits comparten la IRQ 8**; **E–H de 32 bits comparten
  la IRQ 7**.
- El estado de interrupción de A–D vive **dentro de `CON[18:16]`**, con los
  habilitadores en `CON[14:12]` — por eso Rockbox escribe `TBCON = TBCON` para
  acusar recibo, y por eso comprueba `TACON & (TACON >> 4) & 0x7000`.
- El de E–H vive en `TSTAT` (`0x118`), tres bits por canal: E en 24, F en 16,
  G en 8, H en 0. Se limpia escribiendo unos.
- Selector de reloj en `CON[10:8]`: /2, /4, /16, /64, o directo para E–H.
  `CON[6]` elige ECLK (12 MHz, el cristal) frente a PCLK.
- **PCLK no está modelado**: el bloque de reloj es un fichero de registros sin
  lógica, así que no sabemos qué PLL pidió el guest. Los dos timers que usa
  Rockbox eligen ECLK, así que de momento se trata PCLK como ECLK y se avisa
  por `-d unimp` si algún guest lo selecciona de verdad.
- Solo se emula el modo intervalo, que es el único que usa Rockbox. Los demás
  se reportan como no implementados en vez de hacer algo incorrecto en silencio.

#### IRAM0 en la dirección 0

El `crt0.S` de Rockbox lo dice sin ambigüedad:

> *we do not want to execute from 0x0 as iram will be mapped there*
> *copy vector section to start of IRAM0 (it will be mapped to 0x0)*

Así que IRAM0 aparece también en `0x0`, que es de donde el ARM926 toma las
excepciones. Está modelado como alias. **Lo que aún no sabemos** es si la
bootrom ocupa esa dirección primero y el alias solo aparece tras un remap, como
en el S5L8720. Hasta que haya un volcado de bootrom que lo aclare, si se pasa
una bootrom esta se queda con la dirección.

### H2b — Arrancar Rockbox
- [ ] Toolchain: `apt install gcc-arm-none-eabi` sirve para nuestros tests, pero
      Rockbox quiere `arm-elf-eabi-gcc` de `tools/rockboxdev.sh`
- [ ] Compilar el bootloader de Rockbox para ipod6g
- [ ] Lanzarlo con `-kernel` y seguir `-d unimp` como lista de tareas

**Criterio de éxito:** ver la salida de depuración de Rockbox por serie.

### H3 — Pantalla
- [ ] Controlador LCD en `0x38300000` + ventanas en `0x39200000` (IRQ 14)
- [ ] Scanout 320×240 RGB565 a `ui/console`

**Criterio de éxito:** el logo o el menú de Rockbox aparece en la ventana SDL.

### H4 — Entrada
- [ ] Click wheel en `0x3C200000` (IRQ 23), mapeado a teclado/rueda del ratón
- [ ] GPIO de botones

**Criterio de éxito:** navegar por el menú de Rockbox.

### H5 — Almacenamiento
- [ ] Controlador ATA en `0x38700000` (IRQ 29), respaldado por un archivo de
      imagen de disco
- [ ] Alternativa/complemento: CE-ATA sobre SDCI en `0x38b00000` (IRQ 44)

**Criterio de éxito:** Rockbox monta el FAT32 del disco y reproduce un archivo.

### H6 — Audio
- [ ] I²S en `0x3CA00000` + códec CS42L55 por I²C

### H7 (especulativo) — el OS de Apple
- [ ] Modelar el motor AES en `0x38c00000` con la clave GID del S5L8702
- [ ] Bootrom del S5L8702 (hace falta un volcado)
- [ ] Descifrar y arrancar `osos`

Este hito depende de material que ahora mismo no tenemos. Se replantea cuando
lleguemos.

## Estado actual

En H0 completo, empezando H1.
