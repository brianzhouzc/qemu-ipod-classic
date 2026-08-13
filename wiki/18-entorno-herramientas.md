# Entorno y herramientas: qué instalar y descargar para reproducir todo esto

Todo lo que hubo que instalar o descargar para el proyecto entero — desde
descifrar el firmware hasta reproducir audio con Rockbox. Agrupado por para qué
sirve cada cosa. Versiones reales usadas (Ubuntu 24.04 en WSL2).

Para compilar y ejecutar solo la máquina QEMU, basta el apartado 2. El resto se
fue añadiendo según el frente: RE, discos, música, audio, aparato físico.

---

## 1. Base: Windows + WSL2

- **Windows 11**, con **WSL2** y la distribución **Ubuntu-24.04**.
- **WSLg** (viene con WSL2 moderno): da **ventana gráfica** (para ver la pantalla
  del iPod) y **audio** (PulseAudio → altavoces de Windows, que es como se oye la
  música reproducida en el emulador).
- El binario de QEMU es un ELF de Linux: **solo corre dentro de WSL**.
- El árbol vive en `C:\Desarrollo\qemu-ios`, visto desde WSL como
  `/mnt/c/Desarrollo/qemu-ios`. **Aviso:** compilar sobre `/mnt/c` es lento
  (miles de ficheros pequeños sobre el puente de Windows); cada recompilación de
  QEMU son varios minutos por esto.

### En Windows (solo para el aparato físico por USB)

- **usbipd-win** — pasa un dispositivo USB de Windows a WSL. Necesario para hablar
  con el iPod físico (emCORE) desde Linux.
  ```powershell
  winget install --id dorssel.usbipd-win
  # una vez, como administrador:  usbipd bind --busid <n-n>
  # cada conexion:                usbipd attach --wsl --busid <n-n>
  ```

---

## 2. Compilar y ejecutar QEMU

```bash
echo "admin" | sudo -S apt-get install -y \
    ninja-build libglib2.0-dev libpixman-1-dev libsdl2-dev libssl-dev \
    gcc git python3
```

| Paquete | Para qué |
|---|---|
| `ninja-build`, `libglib2.0-dev` | el sistema de compilación de QEMU (meson+ninja) |
| `libpixman-1-dev` | render de framebuffer que QEMU necesita |
| `libsdl2-dev` | la ventana gráfica (display SDL) |
| `libssl-dev` | crypto que QEMU enlaza |
| `gcc`, `git`, `python3` | compilador, control de versiones, meson |

Ver [05-build-notes.md](05-build-notes.md) para el detalle de compilar y las
trampas del entorno (CRLF, symlinks de git, `--disable-werror`).

---

## 3. Ingeniería inversa y depuración

```bash
echo "admin" | sudo -S apt-get install -y gcc-arm-none-eabi gdb-multiarch
```

| Herramienta | Versión | Para qué |
|---|---|---|
| `gcc-arm-none-eabi` (`arm-none-eabi-gcc`, `-objdump`) | 13.2.1 | **desensamblar** el firmware de Apple (`objdump -D -b binary -m arm`), y **compilar Rockbox** (el toolchain oficial de Rockbox no hizo falta) |
| `gdb-multiarch` | 15.1 | depuración con símbolos de Rockbox (backtraces con nombre de función). Ver `tools/ipod_classic/gdbpoke.py` |

`python3` (ya instalado) se usó a base de scripts propios para el análisis del
firmware: `tools/ipod_classic/findlit.py`, `osana.py`, `fwtool.py`, etc.

---

## 4. Imágenes de disco (FAT) y biblioteca de música

```bash
echo "admin" | sudo -S apt-get install -y \
    dosfstools mtools libgpod-common libgpod4t64 libgpod-dev
```

| Paquete | Versión | Para qué |
|---|---|---|
| `dosfstools` (`mkfs.vfat`) | 4.2 | formatear las particiones FAT32/FAT16 del disco del iPod |
| `mtools` (`mcopy`, `mmd`) | 4.0.43 | copiar ficheros a la FAT sin montarla |
| `libgpod-common`, `libgpod4t64`, `libgpod-dev` | 0.8.3 | **escribir el `iTunesDB`** con la firma que el iPod acepta. `libgpod-dev` trae `itdb.h`, contra el que se compila `tools/ipod_classic/addtrack.c`. **No hay CLI ni bindings de Python** en este Ubuntu: hay que llamar a la librería en C |

---

## 5. Audio: reproducir y verificar

```bash
echo "admin" | sudo -S apt-get install -y ffmpeg python3-numpy zip
```

| Paquete | Versión | Para qué |
|---|---|---|
| `ffmpeg` | 6.1.1 | decodificar el MP3 original a PCM de referencia, para **comparar** con lo que capturamos del emulador (correlación cruzada, espectrogramas) |
| `python3-numpy` | 1.26.4 | la medida: RMS, correlación de Pearson, espectro — la prueba de que el WAV **es la canción** y no ruido |
| `zip` | — | empaquetar el build de Rockbox (`rockbox.zip` con los códecs) |

El backend de audio en vivo (altavoces) usa **PulseAudio de WSLg**, ya presente;
QEMU se cablea con `-audiodev pa`.

---

## 6. El aparato físico por USB (emCORE), primera fase

```bash
echo "admin" | sudo -S apt-get install -y python3-usb libusb-1.0-0
```

| Paquete | Versión | Para qué |
|---|---|---|
| `python3-usb` (pyusb) | 1.2.1 | hablar con el **depurador emCORE** del iPod (VID `FFFF`:PID `E000`) para que **el propio aparato descifre el firmware** con su clave GID. Ver [10-classic6g-emcore.md](10-classic6g-emcore.md) y `tools/ipod_classic/emcore_s5l8702.py` |
| `libusb-1.0-0` | 1.0.27 | la capa USB bajo pyusb |

> Los clientes oficiales de emCORE no funcionaban (interfaz `ff/ff/ff` y una API
> de pyusb retirada); se escribió un cliente propio. Detalle en [10](10-classic6g-emcore.md).

---

## 7. Código y firmware descargados (no `apt`)

| Qué | De dónde | Para qué | En el árbol |
|---|---|---|---|
| **Base QEMU-iOS** | `github.com/devos50/qemu-ios` (rama `ipod_touch_2g`) | el punto de partida del emulador | el propio repo |
| **Rockbox** (árbol completo) | rockbox.org / git | especificación ejecutable del hardware S5L8702, **y el firmware libre que reproduce audio** (decodifica en el ARM). Se compila el target `ipod6g` | `assets/rockbox/` |
| **freemyipod/qemu** (rama `s5l8702-dev`) | `github.com/freemyipod/qemu` | modelo previo del S5L8702, referencia | `assets/ref/` |
| **giek2000/ipod-classic-firmware-research** | `github.com/giek2000/...` | research del Classic 6G, guía de descifrado | `assets/ref/giek/` |
| **El firmware** `iPod_24.1.1.2.ipsw` | aportado por el usuario | el `osos` de Apple a descifrar y arrancar | `assets/` (gitignored) |
| Documentación | freemyipod.org/wiki | mapa de memoria, protocolo, bootrom | citada en la wiki |

> `assets/` está **fuera de git**: el firmware de Apple y los volcados del aparato
> no son nuestros para redistribuir.

---

## 8. Todo de una vez (los `apt` de arriba, juntos)

```bash
echo "admin" | sudo -S apt-get update && echo "admin" | sudo -S apt-get install -y \
    ninja-build libglib2.0-dev libpixman-1-dev libsdl2-dev libssl-dev \
    gcc git python3 \
    gcc-arm-none-eabi gdb-multiarch \
    dosfstools mtools libgpod-common libgpod4t64 libgpod-dev \
    ffmpeg python3-numpy zip \
    python3-usb libusb-1.0-0
```

Y en Windows, si se va a usar el iPod físico: `winget install --id dorssel.usbipd-win`.

Con eso y los repos del apartado 7, el entorno reproduce el proyecto entero:
descifrar, arrancar, dibujar, navegar, montar el disco, listar música, compilar
y arrancar Rockbox, y reproducir audio.
