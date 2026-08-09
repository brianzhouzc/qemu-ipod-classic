# Compilación y ejecución en este equipo

## Entorno

- Repo en Windows: `C:\Desarrollo\qemu-ios` (rama `ipod_classic_6g`)
- Compilación en **WSL Ubuntu-24.04**, accediendo por `/mnt/c/Desarrollo/qemu-ios`
- QEMU base: 8.2.0
- 12 cores disponibles

Se compila en WSL y se edita en Windows sobre el mismo árbol. Compilar sobre
`/mnt/c` es más lento que sobre el filesystem nativo de WSL, pero evita tener
dos copias.

## Dependencias

```bash
sudo apt install -y libsdl2-dev libssl-dev libglib2.0-dev libpixman-1-dev \
                    ninja-build python3-venv
```

Ya instaladas: glib 2.80.0, pixman 0.42.2, SDL2 2.30.0, OpenSSL 3.0.13.

## Trampa: finales de línea CRLF

`git clone` en Windows aplica `core.autocrlf=true` por defecto y convierte
**todos** los archivos de texto a CRLF, incluido `configure`. El resultado desde
WSL es:

```
bash: line 1: ../configure: cannot execute: required file not found
```

...que es el shebang `#!/bin/sh\r`. Arreglo, ya aplicado a este repo:

```bash
git config core.autocrlf false
git config core.eol lf
git rm --cached -r . -q
git reset --hard
```

## Trampa 2: symlinks de git en Windows

El árbol de QEMU tiene 7 symlinks, todos en `subprojects/libvduse` y
`subprojects/libvhost-user`. Sin Developer Mode, git en Windows los materializa
como **archivos de texto con la ruta destino dentro**. Desde WSL el compilador
se encuentra un `atomic.h` cuyo contenido es literalmente
`../../../include/qemu/atomic.h` y falla:

```
subprojects/libvduse/include/atomic.h:1:1: error: expected identifier or '(' before '.' token
```

Arreglo aplicado (sustituirlos por copias reales y decirle a git que los
ignore):

```bash
cd /mnt/c/Desarrollo/qemu-ios
git ls-files -s | awk '$1=="120000"{print $4}' | while read -r p; do
  tgt=$(cat "$p"); real="$(dirname "$p")/$tgt"
  rm -rf "$p"; cp -r "$real" "$p"
done
git ls-files -s | awk '$1=="120000"{print $4}' | xargs git update-index --skip-worktree
```

Estos subproyectos (vhost-user/VDUSE) no tienen nada que ver con emular un iPod;
solo estorban.

## Trampa 3: `--disable-werror` no funciona (bug de configure)

`configure` de QEMU 8.2, sobre la línea 1750:

```sh
# Only enable by default for git builds and on select OSes
if test -e "$source_path/.git" && \
    { test "$targetos" = linux || test "$targetos" = "windows"; }; then
    echo 'werror = true' >> $cross
fi
```

Escribe `werror = true` dentro del **native file** de meson
(`build/config-meson.cross`). Una built-in option puesta ahí **gana sobre
`-Dwerror=false`** de la línea de comandos, así que `./configure
--disable-werror` no tiene ningún efecto en un árbol git bajo Linux. Se nota
porque `meson configure` muestra `werror true` para el proyecto principal y
`false` para los subproyectos.

Los modelos de dispositivo de qemu-ios no compilan limpios (variables sin usar,
`hwaddr` impreso con `%x`, parámetros ensombrecidos), así que el bloque está
neutralizado en este fork. Si algún día se quiere `-Werror`, se pide
explícitamente.

Corolario: **después de tocar `Kconfig` o `meson.build`, ninja relanza
`configure` vía `config.status`**, y ahí es donde reaparecía el problema aunque
la compilación inicial hubiera ido bien.

## Comandos

```bash
# Configurar (una vez)
wsl -d Ubuntu-24.04 -e bash -c 'cd /mnt/c/Desarrollo/qemu-ios && mkdir -p build && cd build && \
  ../configure --enable-sdl --disable-cocoa --target-list=arm-softmmu \
               --disable-capstone --disable-slirp \
               --extra-cflags=-I/usr/include/openssl --extra-ldflags=-lcrypto \
               --disable-werror --enable-pie'

# Compilar
wsl -d Ubuntu-24.04 -e bash -c 'cd /mnt/c/Desarrollo/qemu-ios/build && make -j10'
```

El binario queda en `build/qemu-system-arm`.

## Referencia de Rockbox

Copia local de los fuentes del S5L8702 en `assets/ref/`
(nombres aplanados con `__` en vez de `/`). Se descargaron con:

```python
# a partir de https://api.github.com/repos/Rockbox/rockbox/git/trees/master?recursive=1
# filtrando firmware/target/arm/s5l8702/**, firmware/export/config/ipod6g.h,
# firmware/export/i2c-s5l8702.h y bootloader/ipod-s5l87xx.c
```

El mapa de registros completo está en `assets/ref/s5l87xx.h` y su versión resuelta para
S5L8702 en `assets/ref/s5l8702_macros.h`.

## Ejecutar y probar

### La máquina del iPod Video

```bash
./build/qemu-system-arm -M "iPod-Video,entry=0x735a00" \
    -kernel assets/images-5g/osos_full.bin \
    -drive if=ide,index=0,file=assets/ipod-video-disk.img,format=raw,file.locking=off \
    -snapshot -display sdl -serial null
```

- `entry=0x735a00` arranca el bootloader de Rockbox; `entry=0` el retailOS de
  Apple. Es el `entryOffset` del directorio de la partición de firmware.
- `-snapshot` y `file.locking=off` evitan tocar la imagen y permiten varias
  instancias a la vez. Conviene usarlos siempre.
- `-display sdl` abre ventana. Desde WSL funciona por WSLg con `DISPLAY=:0`;
  el binario se compila con `--enable-sdl`, y `-display help` lista los
  backends disponibles.
- Con `remap=on` la DRAM aparece en 0 desde el reset, que es lo que hace falta
  para cargar directamente una `rockbox.ipod` (está enlazada para la dirección
  0 y normalmente depende de que el bootloader monte el mapeo antes).

### Teclas

El teclado está mapeado al click wheel:

| Tecla | Botón |
|---|---|
| `w` | MENU |
| `a` | LEFT |
| `s` | PLAY |
| `d` | RIGHT |
| `Enter` | SELECT |
| `e` | rueda en sentido horario |
| `q` | rueda en sentido antihorario |

La primera pulsación de rueda después de cambiar de sentido no mueve nada:
Rockbox reinicia su acumulador al invertir la dirección.

### Prueba automática de navegación

```bash
./tools/ipod_video/navtest.sh
```

Arranca, sale de *Now Playing* con MENU, gira la rueda, selecciona, y va
capturando la pantalla en cada paso a `assets/nav/*.png`, informando de cuántos
píxeles cambia cada fotograma respecto al anterior.

**Cuidado al interpretar esas cifras.** La pantalla *Now Playing* se anima sola
(el título se desplaza), así que una diferencia de ~2.000 píxeles no prueba
nada. Solo cuenta la barra de selección movida, que son ~14.000.

### Sacar una captura sin ventana

```bash
(sleep 30; echo "screendump /tmp/shot.ppm"; sleep 2; echo quit) \
  | ./build/qemu-system-arm -M iPod-Video,entry=0x735a00 ... \
        -display none -serial null -monitor stdio
```

QEMU escribe PPM. `navtest.sh` lleva dentro un conversor a PNG en Python puro
(solo `zlib`), por si hace falta suelto.

### Averiguar dónde se ha quedado el firmware

En orden de utilidad:

1. **`-d unimp` con recuento.** Casi todos los bloqueos de este SoC son un
   bucle cerrado sobre un registro sin modelar, y salta a la vista:

   ```bash
   ... -d unimp,guest_errors 2>&1 | sort | uniq -c | sort -rn | head
   ```

   > **Aviso**: en un bucle cerrado ese log llega a decenas de millones de
   > líneas y frena tanto la emulación que parece un cuelgue. Antes de concluir
   > que algo no avanza, mide **sin** `-d`.

2. **`-d in_asm`** y mirar los últimos bloques traducidos: es lo más lejos que
   ha llegado el código. Con `--disable-capstone` no hay desensamblado, solo
   `OBJD-T` con los bytes, que se decodifican a mano sin mucho drama.

3. **`info registers` por el monitor**, varias veces seguidas. Si el `R13`
   cambia entre muestras hay varios hilos alternando, o sea que el planificador
   vive y el problema es otro.

4. **Trace points de QEMU**, que para el IDE son oro:

   ```bash
   ... -trace "ide_*" -D /tmp/tr.log
   ```

   `ide_bus_exec_cmd` da los comandos ATA, `ide_sector_read` las lecturas PIO e
   `ide_dma_cb` las de DMA. Ojo: **las transferencias por DMA no emiten
   `ide_sector_read`**; buscar ahí y no encontrarlo no significa que no lea.

5. **`info mtree`** para comprobar que una región está donde crees.
