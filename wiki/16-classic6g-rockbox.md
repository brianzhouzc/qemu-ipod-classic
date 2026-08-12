# Arrancar Rockbox en la máquina iPod-Classic (S5L8702)

Cómo se llevó **Rockbox** (target `ipod6g`) desde "no ejecuta ni una
instrucción" hasta su **menú principal navegable** en la máquina QEMU
`iPod-Classic`. Todo lo que sigue está **medido** (captura mirada o traza)
salvo donde diga `INFERIDO` o `INDETERMINADO`. Las órdenes del final lo
reproducen.

> Por qué Rockbox: el retailOS ya arranca, dibuja y lista la música, pero **no
> suena** porque decodifica el MP3 en el DSP CalmADM2E, que no está modelado
> (ver [14-classic6g-audio.md](14-classic6g-audio.md)). Rockbox decodifica el
> MP3 **en el ARM**, así que el PCM real saldría por el mismo FIFO del I²S que
> ya capturamos a WAV. Este documento es el primer paso: **arrancar Rockbox**.
> Reproducir y capturar el audio es el siguiente frente.

---

## 0. Estado por hito

| Hito | Estado |
|---|---|
| 1. Rockbox carga y ejecuta su primera instrucción | **hecho** — `-kernel rockbox.bin` en `0x08000000` |
| 2. Pasa el init temprano (system, clocks, MMU) sin colgarse | **hecho** — hand-off del bootloader (§2) |
| 3. Inicializa el LCD y DIBUJA | **hecho** — la ruta de píxeles por DMA al LCD (§3) |
| 4. Llega a su menú principal navegable | **hecho** — menú cabbiev2, la rueda mueve el resaltado, SELECT entra (§4) |

Captura del hito 4 (disco con `/.rockbox`, tema cabbiev2 completo): el menú
"Rockbox" con **Files / Database / Resume Playback / Settings / Recording /
Playlists / Plugins / System / Shortcuts**, barra de selección dorada sobre
*Files*. La rueda baja el resaltado un renglón por clic y SELECT sobre
*Database* abre "Database is not ready / Initialize Now" — el comportamiento
correcto de Rockbox. Reproducible con `tools/ipod_classic/rockbox.sh`
(veredicto automático **OK**).

**Regresión:** `navtest.sh` (retailOS 6G) sigue pasando entera después de estos
cambios (VERIFICADO, run completo: rueda 8000 px, SELECT 19340 px, las dos
mitades del menú, MENU vuelve exacto). El 5G no se toca: los únicos ficheros
del modelo tocados son `ipod_classic.c` y `ipod_classic_lcd.c`, ambos del 6G.

---

## 1. La vía: cargar la app directa (estilo osos)

Rockbox se enlaza a DRAM `0x08000000`, entry `start` (`crt0.S`), igual que el
osos del retailOS. La máquina carga un binario RAW con `load_image_targphys`
en `SDRAM_MEM_BASE` y arranca ahí (`ipod_classic.c`). Así que:

```
qemu-system-arm -M iPod-Classic \
    -kernel assets/rockbox/build-full/rockbox.bin \
    -drive if=ide,format=raw,file=<disco.img> -snapshot
```

- Usar el **RAW** `rockbox.bin`, NO el `.elf` (su `e_entry` es `0x0`, la VMA del
  vector table, que sólo se puebla en runtime). VERIFICADO: el primer word del
  `.bin` es `ea00000d = b 0x800003c` (`start: b _newstart`).
- **No** hace falta la cadena del bootloader (`build-boot/bootloader.bin`) para
  el bring-up: la máquina reproduce lo que el bootloader dejaría (§2).

---

## 2. El hand-off del bootloader (hitos 1–2)

En el hardware real, el bootrom + el bootloader de Rockbox/emCORE dejan dos
cosas hechas antes de saltar a la app. En un arranque directo por `-kernel`
las saltamos, y la app se cuelga por ambas. La máquina las reproduce en
`ipod_classic_handoff()` (`hw/arm/ipod_classic/ipod_classic.c`), llamada desde
`mc->reset` **después** de `qemu_devices_reset()`. Gated a que no haya bootrom.

### 2.1. El stack (SP) — el primer cuelgue, MEDIDO

`crt0.S` (rama no-BOOTLOADER) llama `bl memory_init` **antes** de montar sus
propias pilas (`crt0.S:143` frente a `:184`). `memory_init` empieza con
`push {r4, lr}`, así que corre sobre el SP que dejó el bootloader. En un reset
en frío de QEMU **SP = 0**, el `push` escribe en `0xfffffff8` → data abort no
manejado (el vector table en `0x0` aún es ceros) → el PC recorre
`0x10,0x14,...`. Pantalla negra.

**Arreglo:** sembrar `r13` (SVC) con el tope de IRAM0 (`0x22020000`), una pila
scratch como la que deja el bootloader; la app cambia a sus pilas en cuanto
`memory_init` retorna. El retailOS no se ve afectado: monta su propio SP antes
de hacer cualquier `push` (hoy arranca con SP=0, que fallaría igual si no lo
hiciera).

### 2.2. El temporizador de microsegundos (TimerE) — el segundo cuelgue

`USEC_TIMER == TECNT` (TimerE) y `udelay()` gira sobre él sin timeout
(`system-target.h`). Quien arranca TimerE es `usec_timer_init()`, que es
`#ifdef BOOTLOADER` (`clocking-s5l8702.c:381`), así que **la app no lo
arranca**: la primera transferencia I²C de `pmu_preinit()` hace `udelay(5)` y
se cuelga.

**Arreglo:** programar TimerE a 1 MHz exactamente como `usec_timer_init()`:
`TECON=0x440` (ECLK/1, ECLK, modo intervalo), `TEPRE=11` (12 MHz/12 = 1 MHz),
`TEDATA0=~0`, `TECMD=0x3` (CLR|EN), en `TIMER_BASE=0x3C700000`. El retailOS
también maneja TimerE a 1 MHz y lo re-programa (con CLR) antes de usarlo, así
que esto le es transparente — VERIFICADO por `navtest.sh`.

### 2.3. Por qué en `mc->reset` y no en el reset del CPU

El reset del sysbus (que resetea el device del timer) lo registra
`qdev_machine_creation_done()` **después** de que `machine_init` registre
`ipod_classic_cpu_reset`, así que correría el último y **borraría** el TimerE
que programamos. `mc->reset` es el único sitio garantizado a correr tras todos
los resets de device: hace `qemu_devices_reset()` y luego el hand-off.

Con esto Rockbox pasa `memory_init`, `system_init` (i2c/pmu/udelay), gpio,
eic, uart y **llega al LCD** (VERIFICADO por traza `-d unimp`).

---

## 3. La ruta de píxeles: DMA al LCD (hito 3)

El LCD (`0x38300000`) estaba modelado **para el retailOS**, que compone el
frame en el *display pipe* (`0x38900000`) y no empuja ni un píxel por el puerto
de datos. **Rockbox lo hace al revés**: escribe el frame por DMA.

`lcd-s5l8702.c`: `displaylcd_setup()` manda por CPU los comandos `0x2A` CASET /
`0x2B` PASET / `0x2C` RAMWR (params de 4 bytes, escrituras de 32 bits al puerto
`+0x40`, que el modelo aceptaba); luego `displaylcd_dma()` pone modo frame
(`LCD_CON=0x80100db0`) y **DMA los píxeles** con `dmac_ch_queue(...,
S5L8702_DADDR_PERI_LCD_WR, ...)` — DMAC0, `dwidth = WIDTH_16`, a
`LCD_BASE+0x40`.

Medido con `LCDTRACE=1`: **152 escrituras al puerto de datos, 0 píxeles**. Los
píxeles llegan como escrituras de **16 bits**, y el modelo del LCD tenía
`valid.min_access_size = 4` → cada escritura del DMA se rechazaba
("Invalid write at addr 0x40, size 2"). Pantalla negra aunque el driver corría
un update completo.

**Arreglo (`ipod_classic_lcd.c`):** aceptar accesos de media palabra en el
bloque LCD — `valid`/`impl` `min_access_size = 2`, `max = 4`. `panel_data()` ya
toma un píxel de los 16 bits bajos, así que una escritura de 16 o de 32 bits
entrega un píxel. El retailOS sólo usa 32 bits, así que su ruta no cambia
(VERIFICADO por `navtest.sh`).

**El DMA en sí ya funcionaba:** el PL080 (`hw/dma/pl080.c`) transfiere a plena
velocidad las líneas no *paced*, y la línea LCD_WR (id 3) no está marcada como
*paced* (sólo lo está la del I²S). El LLI-chaining del PL080 recorre los ~19
descriptores que Rockbox arma para el frame de 320×240. En cuanto el puerto
acepta los 16 bits, los píxeles aterrizan en `s->fb` y el frame aparece —
MEDIDO: de 0 a 3805 px (fallback) / 74057 px (tema cabbiev2) encendidos.

---

## 4. El disco y el menú (hito 4)

Rockbox monta la partición FAT32 (la máquina interpreta las LBA del MBR en
bloques de 4096 bytes, igual que el retailOS — ver `mkdisk6g4k.py`; por eso un
disco de `mkdisk6g4k.py` sirve tal cual).

- **Con cualquier disco FAT** Rockbox llega al menú, pero enseña primero
  "Installation incomplete" y usa la fuente interna (sysfont). MEDIDO con
  `~/6g/E4kzero_rebuilt.img`: menú de 3805 px, texto blanco sobre negro.
- **Con un disco que lleve `/.rockbox`** carga su tema, fuentes y códecs:
  menú cabbiev2 completo, 74057 px. Fabricado en `~/6g/Erb.img` a partir de
  `assets/rockbox/build-full/rockbox.zip` (contiene `.rockbox/codecs/mpa.codec`,
  el decodificador MP3 en ARM que hará falta para el audio).

Navegación (VERIFICADO, `rockbox.sh`): la barra de selección dorada baja un
renglón por clic de rueda adelante (y 36→52→66) y sube al clic atrás (→52);
SELECT sobre *Database* cambia la pantalla (7666 px distintos) a "Database is
not ready". Los 4 eventos de rueda/botón los registra el modelo de la rueda
(`WHEELTRACE`, IRQ 23). El driver de botones de Rockbox lee el mismo
controlador de rueda que ya modelábamos para el retailOS, sin cambios.

---

## 5. Lo que se stubeó / dejó anotado

- **`ipod_classic_handoff()`**: SP + TimerE de arranque. Es la reproducción del
  hand-off del bootloader, no comportamiento de hardware puro. Anotado en el
  código. Si algún día se modela un bootrom real, el gate `bootrom_path` lo
  desactiva.
- **LCD `min_access_size = 2`**: para aceptar el DMA de 16 bits de Rockbox.
  Anotado; no cambia la ruta de 32 bits del retailOS.
- **Sin tocar Rockbox**: los dos cuelgues se arreglaron en la MÁQUINA, no en el
  firmware. `rockbox.bin`/`.elf`/`.zip` son los que compiló el toolchain
  `arm-none-eabi-gcc 13.2.1` (ver el informe de compilación).

Nada más hizo falta stubear: `storage_init` monta el disco con el driver ATA
propio de Rockbox sobre el modelo ATA existente, `power_init`/`adc`/PMU pasan,
y el kernel (TimerB a 10 kHz) tickea. Lo que aún no se ha ejercitado a fondo es
USB (la app llama `usb_init` tras dibujar; hoy no cuelga) y la reproducción real
(el siguiente frente).

---

## 6. Cómo reproducirlo

```sh
# 1) app de Rockbox (ya compilada); si no, ver el informe de compilación:
#    assets/rockbox/build-full/rockbox.bin  (+ .elf con símbolos, + .zip)

# 2) disco con /.rockbox (tema + fuentes + códecs):
W=$HOME/rbwork; rm -rf $W; mkdir $W; cd $W
unzip -q /mnt/c/Desarrollo/qemu-ios/assets/rockbox/build-full/rockbox.zip
truncate -s 300M $HOME/6g/rbfat.img
mkfs.vfat -F 32 -n ROCKBOX $HOME/6g/rbfat.img
mcopy -s -i $HOME/6g/rbfat.img .rockbox ::
cd /mnt/c/Desarrollo/qemu-ios
python3 tools/ipod_classic/mkdisk6g4k.py $HOME/6g/Erb.img $HOME/6g/fwzero.img $HOME/6g/rbfat.img

# 3) arrancar hasta el menú y comprobar que navega (veredicto automatico):
sh tools/ipod_classic/rockbox.sh $HOME/6g/rockbox $HOME/6g/Erb.img

# ver una captura
python3 tools/ipod_classic/ppm2png.py $HOME/6g/rockbox/0-menu.ppm   # o el .png ya escrito

# 4) diagnostico si se cuelga: donde esta el PC (con simbolos del .elf)
#    lanzar qemu con -gdb tcp::PORT y attach gdb-multiarch; bt.
```

Trazas útiles: `-d unimp,guest_errors` (periférico no modelado), `LCDTRACE=1`
(cada acceso al LCD + totales de píxeles/frames), `WHEELTRACE=1` (rueda),
`ATATRACE=1`, `I2STRACE=1`.

---

## 7. Siguiente frente: que suene

El menú ya está. Para **audio**: entrar en *Files* / *Database*, seleccionar un
MP3, y que Rockbox lo decodifique (`mpa.codec`, en ARM) y saque el PCM por el
I²S. La infraestructura de captura a WAV (`AUDIOWAV=`, FIFO del I²S en
`0x3CA00000`) ya existe (ver [14-classic6g-audio.md](14-classic6g-audio.md)); a
diferencia del retailOS, aquí el PCM debería ser real, no silencio, porque no
depende del DSP no modelado. Riesgos a medir uno a uno: el pacing del DMA de
audio (línea *paced* del I²S) y que la reproducción arranque desde la UI.
