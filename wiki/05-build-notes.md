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
