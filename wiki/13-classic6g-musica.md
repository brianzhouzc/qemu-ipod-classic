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

## 5. Por qué no sale la música: el volumen de datos NO se monta

La primera versión de esta sección culpaba a la firma del `iTunesDB`. **Era
falso, y lo probamos midiendo.** El firmware no llega ni a mirar la base de
datos. La pantalla enseña "No Music" porque **rechaza el volumen FAT32 entero
al leer su cabecera**, mucho antes de la firma.

### Lo que está VERIFICADO, ejecutando

Todo esto es del 2026-08-12, medido, no supuesto:

1. **Se llega al menú de Música.** Navegando con `sendkey` desde el monitor
   (esperar 38 s tras arrancar y espaciar las pulsaciones ~9 s), el retailOS
   entra en Música y dibuja la lista Cover Flow / Playlists / … / Songs con
   **"No Music"** en el panel. Captura mirada, no sólo contada.

2. **La FAT32 se lee UNA vez y se rechaza.** Con `ATATRACE=1` en un arranque
   que llega a Música: de las ~450 lecturas por DMA, **una sola** cae dentro
   de la partición de datos (sus primeros 64 sectores de 512 B = la cabecera),
   y **nunca** se lee el directorio raíz ni `iPod_Control` ni el `iTunesDB`.
   Si el volumen se hubiera montado, el firmware habría leído el directorio
   buscando `iPod_Control`. No lo hace. Se rechaza en la cabecera.

3. **La firma, el hash y el `iTunesDB.p7b` son IRRELEVANTES.** No se llega a
   validar nada de eso porque el volumen no se monta. La sección anterior se
   preocupaba del problema equivocado.

4. **El rechazo no depende de** (todos probados, misma traza de una lectura):
   - tamaño de sector de la FAT: **512, 2048 y 4096** fallan igual;
   - tipo de partición en el MBR: **0x0B (CHS) y 0x0C (LBA)** fallan igual;
   - el campo de sectores ocultos del sector de arranque, puesto a su valor
     correcto.

### Quién lee la cabecera, y la cadena de montaje

Instrumentando el modelo ATA para imprimir el PC y la pila del huésped en la
lectura de la partición de datos (LBA 181568), la lectura sale de una sola
llamada y la cadena, de dentro afuera, es:

```
0x0805a6f4   primitiva de lectura de bloque (registros en 0x38700000)
0x08326518   bucle de lectura de bloques (troceado en sectores)
0x0827a6b0   envoltorio de lectura del sistema de ficheros
0x0824690c   lectura FS -> compara el resultado con 31 en 0x08246948
0x082465xx   DESPACHO VFS: elige el driver por TIPO de sistema de ficheros
             (cmp #1 / #4 / #5, cada uno con su tabla de funciones)
0x0813b20c   tarea de trabajo de RTXC
```

El punto donde se decide aceptar o rechazar está **detrás de la tabla de
funciones** que selecciona `0x082465xx`: el firmware identifica el tipo de
volumen y despacha al driver correspondiente, y ese driver es el que valida (o
descarta) la cabecera FAT.

### Más adentro: la lectura tiene éxito; el rechazo es de contenido

Bajando la cadena a mano (2026-08-12):

- **La lectura del sector de arranque NO falla.** `0x0827a6b0` es la lectura
  FS y devuelve 0 (éxito); su envoltorio `0x0824690c` sólo mapea errores *de
  lectura* (compara el resultado con 31 y 0, códigos de E/S). Así que los bytes
  de la cabecera llegan bien al buffer. Lo que rechaza el volumen es el
  **examen** de esos bytes, no la lectura.

- **El detector de tabla de particiones es `0x0827a980`.** Lee un sector y mira
  su `+0x1FE`: si es `0xAA55` lo trata como **MBR** y salta al parser
  `0x080aee04`; si empieza por `"ER"` lo trata como **APM** (mapa de particiones
  de Apple) y salta a `0x080a38cc`; si ninguno, error. Las direcciones de los
  dos parsers coinciden con las que la §15 del [11](11-classic6g-retailos.md)
  ya había visto ejecutarse.

- El examen que decide FAT-válido-o-no está **detrás del despacho por vtable**
  de `0x082465xx` (elige driver por tipo, `cmp #1/#4/#5`), y ahí es donde se
  cortó esta sesión: resolver ese puntero en ejecución necesita el depurador.

### Lo que falta, acotado para retomarlo

Poner un breakpoint (con `tools/ipod_classic/gdbpoke.py`) en el driver de FAT
que la vtable selecciona en `0x082465xx`, volcar el buffer del sector de
arranque ya leído, y ver **qué campo rechaza**. Es una tarea de una sola
sesión. Candidatos a mirar de camino: el byte que `0x0824659c` lee en `+6` y
compara con `8`, la cadena `"FAT32   "` en `+0x52` del sector de arranque, y —
por descartar— si el firmware corre el detector `0x0827a980` sobre la propia
partición de datos y, al ver el `0xAA55` que todo sector de arranque FAT lleva,
la confunde con un MBR anidado.

Instrumentos que ayudaron y se pueden rehacer (fueron temporales, no están en
el árbol): en `ipod_classic_ata.c`, imprimir en `dma_go` el PC y la pila del
huésped (`current_cpu` → `ARM_CPU(cs)->env.regs[13..15]`,
`cpu_memory_rw_debug`) cuando `ide_get_sector(is) >= 181568`; eso dio la cadena
de montaje entera de un tiro.

> **Hipótesis de trabajo, sin verificar:** que un iPod classic 6G de fábrica
> formatea su partición de datos con parámetros que nuestra `mkfs.vfat` no
> reproduce (alineación, tamaño de FAT, o un campo del BPB que el driver de
> Apple exige). La forma de zanjarlo es el breakpoint de arriba, no seguir
> probando formatos a ciegas: 512/2048/4096 y 0x0B/0x0C ya se descartaron.

### Segunda incógnita, independiente: que SUENE

Aunque la pista aparezca en la lista, reproducirla es otro problema. El I²S no
está modelado —hoy se tiran al suelo unos 6 000 accesos por arranque— así que
el audio es un frente aparte. Ver [04-roadmap.md](04-roadmap.md).
