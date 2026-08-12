# Meterle música al Classic 6G emulado

Escrito el 2026-08-11, después de que el retailOS arrancara, dibujara y se
navegara. La pregunta siguiente era la obvia: *¿y si le metemos una canción?*

Lo que sigue está **VERIFICADO** salvo donde diga otra cosa. La parte que
quedó sin comprobar está en el apartado 5, y es la que decide si esto sirve.

## 1. Por qué no basta con copiar el MP3

El iPod no recorre el disco buscando ficheros de audio. Lee **su base de
datos**, el `iTunesDB` de `iPod_Control/iTunes/`, y enseña lo que ahí ponga.
Un disco lleno de MP3 sin base de datos da una biblioteca vacía.

Y desde esta generación —los modelos de 2007, que incluyen el classic 6G y el
nano 3G— Apple **firma esa base de datos** con un hash que depende del
identificador FireWire del aparato. Es lo que durante años impidió que las
herramientas de terceros escribieran música en estos modelos. `libgpod` acabó
implementándolo, y es la única implementación libre que lo hace.

## 2. Los datos del aparato salen de la NOR

`assets/classic6g/bootflash_nor_1MB.bin` empieza con el bloque `SysCfg`, con
las etiquetas de cuatro letras **invertidas** — por eso una búsqueda de
`"SCfg"` no encuentra nada y una de `"gfCS"` sí. Es la misma inversión que
usan las entradas del directorio de imágenes del firmware
([01-firmware-ipsw.md](01-firmware-ipsw.md)).

| Offset | Etiqueta | Invertida | Valor |
|---|---|---|---|
| `0x0000` | `gfCS` | `SCfg` | cabecera del bloque |
| `0x0018` | `mNrS` | `SrNm` | el número de serie del aparato |
| `0x0090` | `NBLM` | `MLBN` | el número de placa |
| `0x00a4` | `#doM` | `Mod#` | `MB147` |

`MB147` es justo lo que `libgpod` llama *model number* y con lo que decide qué
iPod está inicializando. Los otros dos son identificativos del aparato del
usuario, así que **no se transcriben aquí**; están en el volcado, que está
fuera de git.

**Lo que NO está en el `SysCfg`: el identificador FireWire.** Se buscó y no
aparece. Ver el apartado 5.

## 3. La receta

`libgpod` viene en Ubuntu pero **sin herramienta de línea de órdenes** que
añada pistas — `libgpod-common` sólo trae `ipod-read-sysinfo-extended` e
`ipod-time-sync`, y `gtkpod` no está empaquetado. Hay que llamar a la
librería. `libgpod-dev` trae `itdb.h`.

```bash
echo "admin" | sudo -S -p "" apt-get install -y libgpod-common libgpod4 libgpod-dev
# python3-gpod NO existe en este Ubuntu: los bindings no son una opción
```

Un programa de cincuenta líneas contra `itdb.h` basta: `itdb_init_ipod()`,
`itdb_parse()`, `itdb_track_new()` + `itdb_track_add()` +
`itdb_playlist_add_track()` a la lista maestra, `itdb_cp_track_to_ipod()`, y
`itdb_write()`.

**`itdb_init_ipod()` falla de entrada** con `Couldn't find the iPod firewire
ID` si no hay `SysInfo`. Hay que escribirlo antes, a mano:

```
iPod_Control/Device/SysInfo
    ModelNumStr: xMB147
    FirewireGuid: 000A270019B4C3D2
    pszSerialNumber: <el del SysCfg>
```

Con eso escribe la biblioteca sin protestar:

```
iPod_Control/Music/F21/libgpod408455.mp3
iPod_Control/iTunes/iTunesDB          6330 bytes, 1 pista
iPod_Control/Artwork/ArtworkDB
iPod_Control/Device/SysInfo
```

Y el disco completo se monta como siempre, con la FAT32 ya poblada:

```bash
python3 tools/ipod_classic/mkdisk6g4k.py ~/6g/Emusic.img \
        assets/extracted/Firmware-24.9.1.2 ~/6g/fat32m.img
```

## 4. Cuidado al arrancarlo a mano

`tools/ipod_classic/navtest.sh` hace `pkill -9 -x qemu-system-arm` antes de
cada carrera, y `-x` es coincidencia **exacta de nombre**. Una copia del
binario con otro nombre sobrevive — cosa útil para que una sesión no mate el
QEMU de otra, y molesta cuando ese superviviente se come un núcleo y hace que
el huésped pierda sus propios plazos de arranque y se reinicie a *diskmode*.

Eso ya costó cuatro carreras, y por eso `navtest.sh` ahora cuenta procesos
sueltos y avisa antes de empezar. Si arrancas una copia con otro nombre para
mirar la pantalla, **mátala tú** antes de fiarte de una carrera de pruebas.

## 5. Lo que queda sin comprobar, y es lo importante

**El identificador FireWire de arriba está inventado.** No aparece en el
`SysCfg`, así que se puso uno cualquiera para que `libgpod` firmara.

El firmware valida el hash contra **su propio** identificador, no contra el
del fichero. Si no coinciden, lo esperable es que el menú de Música aparezca
y la lista salga **vacía**. Eso no sería un fallo del disco ni de la base de
datos: sería la firma.

Si pasa, lo siguiente es averiguar **de dónde saca el firmware su
identificador** —desensamblando quién lee qué antes de validar— y ponerlo
igual en `SysInfo`. En una emulación tenemos los dos lados, así que el
problema es de averiguación, no de criptografía.

> **INFERIDO, no verificado:** que el retailOS del 6G rechace una base de datos
> mal firmada mostrando una lista vacía. Es el comportamiento que se documenta
> para estos modelos, pero no lo hemos visto aquí.

Y hay una segunda incógnita, independiente de la primera: **que suene**. El
I²S no está modelado —hoy se tiran al suelo unos 6 000 accesos por arranque—
así que aunque la canción aparezca en la lista, reproducirla es otro
problema. Ver [04-roadmap.md](04-roadmap.md).
