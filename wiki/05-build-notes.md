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
                    ninja-build python3-venv \
                    dosfstools mtools
```

Ya instaladas: glib 2.80.0, pixman 0.42.2, SDL2 2.30.0, OpenSSL 3.0.13.
Verificadas el 2026-08-09, junto con gcc 13.3.0, Python 3.12.3, ninja 1.11.1 y
meson 1.3.2 (en `build/pyvenv/`).

`dosfstools` y `mtools` **no** son de QEMU: los necesita `mkdisk.sh` para
construir la FAT32 de la imagen de disco. `python3` lo usan `navtest.sh`,
`appletest.sh`, `fwtool.py`, `extract-vc.py` y el propio `mkdisk.sh`.

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

### La trampa no está cerrada del todo

`core.autocrlf=false` impide que **git** convierta, pero **no impide que un
editor de Windows escriba CRLF**. Contados el 2026-08-09, **tres ficheros
versionados estaban al 100 % en CRLF**, violando el `.editorconfig` del propio
repo (línea 16: `end_of_line = lf`):

| Fichero | Situación |
|---|---|
| `hw/arm/meson.build` | **De QEMU upstream.** Era LF hasta `fb7c1039a` y pasó a CRLF en `5b5af8966`. Resultado: **añadir una sola línea a la lista de fuentes produjo un diff de 77 insertions + 77 deletions**. **Convertido a LF**; verificado que el contenido no cambia |
| `hw/arm/ipod_video/ipod_video.c` | Nuestro. 1384 líneas. Se deja como está |
| `include/hw/arm/ipod_video/ipod_video.h` | Nuestro. 364 líneas. Se deja como está |

El caso que duele es el primero: es un fichero upstream, así que cualquier
rebase o diff contra QEMU arrastra ese ruido. Los dos nuestros son cosméticos y
se dejan para no meter un diff de 1.700 líneas de puro espacio en blanco en
mitad de otro trabajo; si se convierten algún día, que sea en un commit propio
y sólo de eso.

> **Para contarlos, `grep -c $'\r'` no vale** en Git Bash: da falsos positivos.
> Esto sí:
>
> ```bash
> for f in $(git ls-files); do
>   n=$(tr -cd '\r' < "$f" | wc -c)
>   [ "$n" -gt 0 ] && echo "$n CR  $f"
> done
>
> sed -i 's/\r$//' <fichero>   # convertir
> ```

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
  # Ya convertido (skip-worktree): no tocarlo. Ver el aviso de abajo.
  [ "$(git ls-files -v -- "$p" | cut -c1)" = "S" ] && continue
  tgt=$(cat "$p"); real="$(dirname "$p")/$tgt"
  rm -rf "$p"; cp -r "$real" "$p"
done
git ls-files -s | awk '$1=="120000"{print $4}' | xargs git update-index --skip-worktree
```

> **Aviso: esto se ejecuta UNA vez, sobre un clon recién hecho.** El snippet
> original **no era idempotente y volver a lanzarlo borraba los ficheros**.
> Comprobado el 2026-08-09 sobre una copia: una vez convertidos, `cat "$p"` ya
> no devuelve la ruta destino sino los 14 KB de código C, así que `cp` falla
> con *"File name too long"*… **pero el `rm -rf` de la misma línea sí se
> ejecuta**. Y como están en `skip-worktree`, `git checkout` tampoco los
> restaura. La guarda `[ ... ] = "S"` de arriba es lo que lo evita.

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
wsl -d Ubuntu-24.04 -e bash -c 'cd /mnt/c/Desarrollo/qemu-ios/build && ninja qemu-system-arm'
```

El binario queda en `build/qemu-system-arm`.

`make -j10` también funciona —delega en ninja—, pero añade unos 8 s de
`meson introspect`; `ninja qemu-system-arm` es la vía directa.

### Cuánto tarda, y una trampa que parece un build roto

Medido el 2026-08-09 en este equipo, sobre un `build/` limpio:

| Operación | Tiempo |
|---|---|
| `configure` + `ninja` **desde cero** | **16 min** (configure ~3, ninja ~13) |
| `ninja` sin nada que hacer, **en caliente** | 21 s |
| `ninja` sin nada que hacer, **primera vez tras editar desde Windows** | **6 min** |
| Recompilar un `.c` y enlazar | ~3 min |

> **Ese "no-op" de seis minutos no es un build roto: es el filesystem.** WSL
> tiene que hacer `stat` de todo el árbol a través de 9p, y la primera pasada
> tras haber tocado ficheros desde Windows es lentísima. La segunda baja a 20 s.
> Sin saber esto es fácil concluir que algo va mal.

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
  para cargar directamente la aplicación de Rockbox (está enlazada para la
  dirección 0 y normalmente depende de que el bootloader monte el mapeo antes).
  Llega al menú en ~20 s en vez de 28.

  > **Corregido**: aquí ponía "cargar directamente una `rockbox.ipod`". **No
  > sirve tal cual**: hay que quitarle antes su cabecera de 8 bytes (checksum
  > más el target id `"ipvd"`). Probado el 2026-08-09 — con la cabecera puesta
  > la pantalla se queda negra, 0 píxeles. Lo que arranca es
  > `assets/images-5g/rockbox_app.bin`, que es `rockbox.ipod[8:]`.

### Propiedades de la máquina

`entry`, `remap` y `semihosting` se registran en `instance_init`, así que **no
aparecen en `-M iPod-Video,help`**, que sólo lista las propiedades de clase.
Quien no lea el fuente o esta wiki no las encuentra.

| Propiedad | Por defecto | Qué hace |
|---|---|---|
| `entry` | `0` | Offset dentro del `osos` donde empieza la ejecución |
| `remap` | `off` | DRAM en la dirección 0 desde el reset |
| `semihosting` | `on` | Contesta al semihosting de ARM; es como el retailOS imprime sus panics |

### La máquina del iPod classic 6G

Hacen falta el `osos` **descifrado** (ver [10](10-classic6g-emcore.md)) y un
disco construido a partir de la partición de firmware del `.ipsw`:

```bash
# el disco. El FAT32 va VACIO: las tipografias estan en la particion de
# FIRMWARE, no aqui.
truncate -s 64M ~/6g/fat32.img && mkfs.vfat -F 32 -n IPOD ~/6g/fat32.img
python3 tools/ipod_classic/mkdisk6g4k.py ~/6g/E.img \
        assets/extracted/Firmware-24.9.1.2 ~/6g/fat32.img

cd build && ./qemu-system-arm -M iPod-Classic \
    -kernel ../assets/classic6g/osos_decrypted.bin -serial null \
    -drive if=ide,format=raw,file=$HOME/6g/E.img -snapshot
```

- Tarda **entre 22 y 30 s** de reloj de pared hasta el primer píxel.
- **Sin `-drive` no hay imagen**, y eso es del huésped: la interfaz se muere
  buscando tipografías y entra en un ciclo de caída y reinicio. No hay ninguna
  tipografía dentro de la imagen del firmware.
- **No hay propiedad `entry`**: el `osos` descifrado se carga crudo en
  `0x08000000`, que es su `addr` en el directorio del firmware. Las dos
  propiedades que sí hay, y que tampoco salen en `-M iPod-Classic,help`, son
  `bootrom` (ruta a un binario de bootrom, que **no tenemos**) y `semihosting`
  (encendida por defecto — **es como el retailOS imprime sus panics**).

Variables de entorno de traza, todas del Classic:

| Variable | Qué traza |
|---|---|
| `DISPTRACE=1` | el compositor de `0x38900000`, con el PC del huésped y totales |
| `LCDTRACE=1` | el panel: órdenes, píxeles y arranques del motor de trama |
| `ATATRACE=1` | toda la conversación con el ATA, registro a registro |
| `WHEELTRACE=1` | paquetes, órdenes y flancos de la IRQ 23 |
| `GPIOTRACE=1` | accesos a GPIO |
| `IPOD_WHEEL_STEP` (6), `IPOD_WHEEL_TICK_MS` (40), `IPOD_WHEEL_UNTOUCH_MS` (1500) | ajustan la rueda. **Ninguno es un dato del aparato** |

### Teclas

El teclado está mapeado al click wheel, **igual en las dos máquinas**:

| Tecla | Botón |
|---|---|
| `w` | MENU |
| `a` | LEFT / anterior |
| `s` | PLAY |
| `d` | RIGHT / siguiente |
| `Enter` | SELECT |
| `e` | rueda en sentido horario |
| `q` | rueda en sentido antihorario |

En el 5G, la primera pulsación de rueda después de cambiar de sentido no mueve
nada: Rockbox reinicia su acumulador al invertir la dirección. En el Classic no
pasa eso — cada pulsación mueve un elemento — porque el modelo espacia los
paquetes con un temporizador para satisfacer al decodificador de Apple, que
exige un paquete de armado antes de cada movimiento.

### Pruebas automáticas de navegación

Hay **tres**. Las tres informan de cuántos píxeles cambia cada fotograma
respecto al anterior; las dos del 5G capturan a `assets/` y la del Classic a
`~/6g/nav`.

```bash
./tools/ipod_video/navtest.sh       # 5G, Rockbox, ~55 s
./tools/ipod_video/appletest.sh     # 5G, el retailOS de Apple, ~3 min
sh tools/ipod_classic/navtest.sh    # Classic, el retailOS, ~2 min
```

**La del Classic sí es una aserción**, a diferencia de las otras dos: comprueba
que la pantalla está quieta antes de tocar nada, recorre la pantalla de idiomas
(`e`, `e`, `q`, Enter) y **sigue hasta el menú principal**, donde vuelve a
comprobar que está quieta, gira, entra en Música con Enter y sale con `w`.
**Suspende** si algún giro no mueve el resaltado, si el suelo de ruido se pasa
de **200 px** —que es la premisa de la que cuelgan todos sus umbrales—, o si
MENU no devuelve **el mismo fotograma** del que se salió. Deja los PNG en
`~/6g/nav` y **hay que mirarlos**, que es la otra mitad de la medida.

Cifras de referencia, **de después de la puerta de capas del §18 de
[11](11-classic6g-retailos.md)**: **8 000 px por giro** sobre un suelo de
**0 px**, **~19 340 px** al entrar con Enter desde la pantalla de idiomas,
**~7 950 px en la lista + ~7 180 px en el panel** por giro en el menú principal,
y **menos de 1 000 px** entre el fotograma al que MENU vuelve y el que se dejó.
Las cifras viejas que circulan por ahí —**76 800 px encendidos y 50 540 al
seleccionar**— son de **antes** de esa puerta, cuando una capa muerta tapaba la
interfaz: no son una regresión, son otra imagen.

Y **una carrera de cada tres o cuatro suspende sola** porque el huésped tira un
SELECT, sin que nada esté roto; la cabecera del script dice cómo reconocerlo.
Antes de creerse un fallo: `ps aux | grep qemu` y `uptime`, porque con la
máquina cargada el huésped se reinicia a *diskmode* y el script avisa a gritos
de que el veredicto no vale.

`navtest.sh` arranca, sale de *Now Playing* con MENU, gira la rueda y
selecciona. `appletest.sh` hace dos arranques del retailOS: uno que fotografía
la pantalla a 10, 20, 40, 60, 90 y 120 s para probar que la interfaz sale y se
queda, y otro que recorre idioma → menú principal → *Videos* → atrás.

**Cuidado al interpretar las cifras.** La pantalla *Now Playing* de Rockbox se
anima sola (el título se desplaza), así que una diferencia de ~2.000 píxeles no
prueba nada. Sólo cuenta la barra de selección movida, que son ~14.000, o una
pantalla nueva, que pasa de 18.000. Un puñado de píxeles es el reloj o el icono
de batería.

> **Ninguno de los dos scripts del 5G es una aserción: son generadores de
> evidencia.** Los dos cierran la invocación de QEMU con `|| true` y su
> post-proceso en Python no comprueba nada. Verificado: si no se genera ningún
> PPM, el bucle no imprime nada **y el script sale con código 0**. Un QEMU que
> muera al arrancar da un "pase" silencioso. Hay que mirar las cifras, o
> ponerles un umbral.

> **Los fotogramas de Rockbox no son reproducibles bit a bit** contra una línea
> base guardada, porque su barra de estado lleva reloj: cambian un par de
> cientos de píxeles según la hora. Los del retailOS **sí** lo son, porque no
> dibuja reloj.

El `-semihosting-config enable=on,target=native` que lleva `appletest.sh`
dentro es **redundante**: la máquina activa el semihosting por su cuenta.
Comprobado quitándolo — la captura sale con el mismo hash. El comentario del
script sigue siendo buen consejo (sin semihosting los panics son silenciosos),
pero el flag no hace falta.

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
