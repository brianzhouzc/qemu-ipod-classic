# El iPod classic 6G real: hablar con emCORE y descifrar su firmware

Sesión del 2026-08-09 contra el aparato físico del usuario. El resultado
importante: **el `osos` de Apple está descifrado**. Era el bloqueo fundacional
del proyecto, documentado en [01-firmware-ipsw.md](01-firmware-ipsw.md)
(entropía 7,99 bits/byte, cero cadenas ASCII).

Herramienta escrita en esta sesión: `tools/ipod_classic/emcore_s5l8702.py`,
con pruebas en `tools/ipod_classic/test_emcore_s5l8702.py`.
Los volcados van a `assets/classic6g/`, que está **gitignorado a propósito**:
son binarios de Apple y no son nuestros para redistribuir.

## 0. Qué corre el aparato — VERIFICADO

No tiene disco duro. Arranca a emCORE, el depurador de freemyipod, que expone
un interfaz USB vendor-specific.

```
Bus 001 Device 002: ID ffff:e000 freemyipod.org emCORE Debugger
```

Respuesta al comando de identificación (comando 1, subtipo 0):

| Campo | Valor |
|---|---|
| Versión | **v0.2.3 r859** |
| `swtypeid` | 2 = *emCORE Debugger* |
| `hwtypeid` | `0x4C435049` = `"IPCL"` en little-endian = iPod Classic |
| Límites de paquete | `cout`/`cin` 512 B, `dout`/`din` 262144 B |
| Malloc pool | `0x08000000` – `0x0BFC0000` (63,75 MiB de DRAM libre) |

r859 (2012-01-02) es la última *release* oficial, así que es lo que tiene
cualquiera que instalara emCORE por el cauce normal. Esto importa mucho: el
protocolo cambió por completo después.

## 1. El protocolo (pre-r891)

En SVN **r891** emCORE tiró su pila USB y pasó a hacer el depurador por
transferencias de control sobre EP0. Todo lo de aquí es el protocolo
**anterior**, que es el que habla este aparato.

Cuatro endpoints bulk, asignados por **orden de aparición en el descriptor**
(`emcore/trunk/usb/usb.c:696-699`):

| Índice | Uso | En este aparato |
|---|---|---|
| 0 | command OUT | `0x02` |
| 1 | command IN | `0x81` |
| 2 | data OUT (DMA) | `0x04` |
| 3 | data IN (DMA) | `0x83` |

Cabecera de **16 bytes**, todo little-endian. En el comando, los 4 primeros
bytes son el ID; en la respuesta, el código de estado:
`0` inválido, `1` OK, `2` no soportado, `3` ocupado.

Formatos de respuesta que hacían falta y que están fijados en los tests:

```
cmd 1/0 (versión)      <I I B B B B I   status, revision, major, minor, patch, swtypeid, hwtypeid
cmd 1/1 (packetsize)   <I H H I I       status, coutmax, cinmax, doutmax, dinmax
cmd 1/2 (malloc pool)  <I I I I         status, lower, upper, sin usar
```

Lectura de memoria: comando **4** por el command pipe (máximo
`cin` − 16 = 496 B por transacción) o comando **6** por el data pipe con DMA
(hasta 256 KiB). La ruta DMA exige alineamiento a 16 bytes, así que las
peticiones se parten en (cabeza / cuerpo alineado / cola) — es el
`_alignsplit` de `libemcore`, replicado y con test propio.

Medido: **~8 MiB/s** de extremo a extremo escribiendo, cifrando y leyendo.

### Los clientes oficiales no funcionan tal cual — VERIFICADO

| Cliente | Qué pasa |
|---|---|
| HEAD | Sólo acepta interfaces `ff/00/00`; el aparato es `ff/ff/ff`, así que ni lo enumera (`libemcore.py:1172`). Y usaría EP0, que este emCORE no implementa. |
| r859 | Llama a `dev.write(ep, data, interface, timeout)`. El parámetro `interface` desapareció en pyusb 1.0.0b1 → `TypeError` con cualquier pyusb moderno. |
| ambos | `misc.py:364` usa `inspect.getargspec`, eliminado en Python 3.11. |

Por eso `emcore_s5l8702.py` habla el protocolo directamente y no depende del
árbol de emCORE. Sólo implementa comandos de lectura más lo imprescindible
para el AES; **no** implementa escritura de bootflash (23), `execimage` (21),
`runfirmware` (24) ni nada de almacenamiento/NAND. Las escrituras a memoria se
rechazan fuera del malloc pool.

## 2. El descifrado del firmware — HECHO, VERIFICADO

La clave GID vive en el silicio y no se puede extraer. Lo que sí se puede es
pedirle al propio aparato que descifre: comando **25**, que expone el motor AES
del S5L8702.

```
offset 0  (4B) ID = 25
offset 4  (1B) 0 = descifrar, 1 = cifrar
offset 5  (1B) cero
offset 6  (2B) índice de clave
offset 8  (4B) dirección del búfer (in situ)
offset 12 (4B) tamaño
```

Índices de clave (Rockbox, `firmware/target/arm/s5l8702/crypto-s5l8702.h:83-87`):
**1 = GKEY (la GID, por modelo)**, 2 = UKEY (única por aparato).

Confirmado experimentalmente cuál es la buena, sobre 4 KiB del `osos`:

| keyidx | Entropía resultante | Palabras con `cond=0xE` | Veredicto |
|---|---|---|---|
| — (cifrado) | 7,953 | — | — |
| **1 (GID)** | **5,679** | **73,9 %** | **código ARM** |
| 2 (UID) | 7,954 | 6,0 % | ruido |

### El detalle que hace falta para trocear: el IV se reinicia

`hwkeyaes()` pone `AESIV[0..3] = 0` **al principio de cada llamada**
(`emcore/target/ipodnano3g/hwkeyaes.c`, idéntico en Rockbox). Es AES-128-CBC
con IV = 0 por invocación.

Trocear ingenuamente da mal el **primer bloque de cada trozo**. Como en CBC
`P[i] = D(C[i]) ^ C[i-1]`, la reparación de la costura es exacta:

```
P[0]_bueno = P[0]_devuelto ^ (último bloque de cifrado del trozo anterior)
```

Comprobado contra el aparato: descifrar `[0,0x2000)` de una vez, frente a
descifrar `[0,0x1000)` y `[0x1000,0x2000)` con la reparación, da **byte a byte
lo mismo**. Hay además un test offline con AES por software.

El tamaño debe ser múltiplo de 16; los bytes sobrantes al final se copian tal
cual (el `osos` mide `0x976F7C`, que deja 12 bytes de cola).

### Resultado

```bash
python3 tools/ipod_classic/emcore_s5l8702.py decrypt \
        assets/images/1_osos.payload assets/classic6g/osos_decrypted.bin
```

| Fichero | Tamaño | Entropía antes → después | SHA-1 del claro |
|---|---|---|---|
| `osos_decrypted.bin` | 9 924 476 | 8,000 → **6,005** | `18d09b8928f4b0b6f1d500ab5a31bdf76f294635` |
| `aupd_decrypted.bin` | 1 164 356 | — → **4,529** | `52e0cc6d868c962e8b23103d6bf0942130c29303` |

Los 9,9 MB tardaron **1,2 segundos**. (La guía de giek habla de ~2 horas: eso
es la ruta wInd3x por DFU, que es otra cosa. Aquí el aparato ya está corriendo
emCORE y el motor AES va por DMA.)

Que el claro es de verdad el retailOS de Apple, no una casualidad estadística:

- Vectores ARM en `0x0`: ocho ramas `b`, con destinos coherentes
  (`0x8834` el reset, y `0x33E8`–`0x3410` los handlers de excepción).
- `Copyright 2001-2008 Apple Inc. All rights reserved.`
- La interfaz literal: `Cover Flow`, `Playlists`, `Artists`, `Albums`,
  `Compilations`, `Shuffle Songs`, `Now Playing`.
- Nombres de clase de Apple: `TSilverMediaListCntlr_Playlists`,
  `TPhotosSettingsCntlr`, `HandleNikePlusSelected`.
- `RTXC v3.2b for ARM and Thumb - ARM ADS 1.0.1` — el RTOS.
- Licencias de OpenSSL, FreeType y Eric Young.
- 42 014 cadenas ASCII de ≥8 caracteres, frente a **0 reales** antes.

Y en el `aupd`, las rutas de compilación de la propia Apple:

```
c:\bwa\N25FirmwareWin-435\srcroot\Firmware\BootLoader\btMm.c
/cygdrive/c/bwa/ImosEFIWin-120/srcroot/BuildResults/N25/Release/ARM/SecCore.stripped
```

`N25` es el nombre en clave del iPod classic.

## 3. La NOR SPI — VERIFICADO

Comando **22** (`bootflashread`) copia flash → RAM. Emite el opcode SPI 3
(READ DATA): no puede modificar nada. 1 MiB volcado,
SHA-1 `46aa43136275f7abfe65b95c6752f358d7f0c835`.

De `0x090000` en adelante está todo a `0xFF` (borrado).

### Bloque de configuración `SCfg` en `0x0000`

Entradas de 20 bytes. La etiqueta de 4 caracteres está almacenada **al revés**,
porque en el código es una constante `uint32` little-endian: en el fichero se
lee `gfCS` y la etiqueta es `SCfg`. Los *valores* van en orden normal.

| Etiqueta | Significado | Valor en este aparato |
|---|---|---|
| `SCfg` | cabecera | tamaño `0xCC`, 9 entradas |
| `SrNm` | número de serie | *(identificativo — está en el volcado)* |
| `FwId` | id de firmware | `0x13118B17`, `0x000A2700` |
| `HwId` | id de hardware | `0x82021685` |
| `HwVr` | versión de hardware | `0x00130000` |
| `Codc` | códec de audio | `"SB"` |
| `SwVr` | versión de software | `"1.0"` |
| `MLBN` | nº de placa base | *(identificativo — está en el volcado)* |
| `Mod#` | modelo Apple | `"MB147"` |
| `Regn` | región | `0x00020001` |

### El directorio de emCORE en `0x1000`

Entradas de 16 bytes: nombre de 8 caracteres, offset, tamaño. Es la instalación
de emCORE del usuario, no material de Apple.

| Nombre | Offset | Tamaño |
|---|---|---|
| `libboot` | `0x02000` | `0x002CE` |
| `libpng` | `0x03000` | `0x0131E` |
| `umsboot` | `0x05000` | `0x02362` † |
| `emcore` | `0x0A000` | `0x0AE02` † |
| `libui` | `0x15000` | `0x018C9` |
| `libmkf32` | `0x17000` | `0x00339` |
| `bootmenu` | `0x18000` | `0x24091` |
| `rockbox` | `0x3D000` | `0x4C72E` † |

† tienen el bit 31 puesto en el campo de tamaño. **No sé qué significa** — no
lo he verificado contra el código.

### Una imagen `8702` de Apple sin descifrar, en `0x8000`

Cae en el hueco entre `umsboot` (acaba en `0x7362`) y `emcore` (`0xA000`), y
**no está en el directorio de emCORE**. La cabecera es el mismo contenedor
`8702`/`1.0` que el `osos` del `.ipsw`, pero de tipo `0x01` en vez de `0x03`:

```
@0x008000  magic='8702' ver='1.0' type=0x01  paysize=0xE10 (3600 bytes)
```

INFERIDO, no verificado: es lo que queda del cargador NOR original de Apple.
**Queda pendiente descifrarlo** con la misma clave GID — debería ser directo.

## 4. La bootrom NO se puede leer — hallazgo negativo, VERIFICADO

`0x20000000` no devuelve la bootrom. Devuelve cuatro constantes, una por cada
banco de 16 KiB, y el patrón se repite cada 64 KiB:

| Rango | Valor devuelto | Como instrucción ARM |
|---|---|---|
| `0x20000000`–`0x20003FFF` | `0xE3A01A01` | `mov r1, #0x1000` |
| `0x20004000`–`0x20007FFF` | `0x2000C094` | (un puntero dentro de la propia ventana) |
| `0x20008000`–`0x2000BFFF` | `0xE3520004` | `cmp r2, #4` |
| `0x2000C000`–`0x2000FFFF` | `0x00000000` | — |

Comprobado que **no es un artefacto del cliente**: leer 4 bytes en direcciones
consecutivas devuelve el mismo valor una y otra vez, mientras que el mismo
método sobre la SRAM en `0x22000000` devuelve la tabla de vectores real de
emCORE. Sólo los bits 14-15 de la dirección influyen; todo lo demás se ignora.

Interpretación (INFERIDA): la bootrom está **bloqueada tras el arranque** y el
bus entrega un valor residual por banco. Que dos de los cuatro valores sean
instrucciones ARM válidas y el tercero un puntero al propio rango sugiere que
son restos reales de su contenido, pero **no son un volcado utilizable**.

Esto explica por qué no existe ningún volcado público de la bootrom del S5L8702.
Sacarla necesitaría otra vía (DFU + exploit tipo wInd3x), no emCORE.

## 5. Cómo colgué el aparato — evitar

Leer **`0x20100000`** (justo por encima de la ventana espejada de la bootrom)
tumbó el hilo del monitor. El aparato sigue enumerado en USB — el descriptor se
lee perfectamente — pero no responde a ningún comando: todo da
`USBTimeoutError`. Casi seguro un data abort en el handler de lectura.

emCORE tiene handlers de aborto (las cadenas `Data abort at %08X!` y
`Prefetch abort at %08X!` están en el volcado de SRAM), pero no salvan al
monitor.

**Regla práctica: no leer direcciones no documentadas.** El mapa que se sabe
bueno es DRAM `0x08000000`, SRAM `0x22000000`, y los periféricos que aparecen
en `firmware/export/s5l87xx.h`.

Se recupera con un reinicio manual del aparato (MENU+SELECT). No lo he hecho yo:
no toco el aparato del usuario más allá de leer.

## 6. Lo que se leyó, y lo que queda

En `assets/classic6g/` (gitignorado):

| Fichero | Tamaño | Qué es |
|---|---|---|
| `osos_decrypted.bin` | 9 924 476 | **el retailOS de Apple en claro** |
| `aupd_decrypted.bin` | 1 164 356 | el actualizador en claro |
| `bootflash_nor_1MB.bin` | 1 048 576 | la NOR SPI entera |
| `sram_22000000.bin` | 245 760 | los 240 KiB de SRAM (emCORE en ejecución) |
| `probe_20000000_64k.bin` | 65 536 | la prueba de la bootrom bloqueada |

Pendiente:

- Descifrar la imagen `8702` de `0x8000` de la NOR (3600 bytes). Necesita el
  aparato despierto.
- La bootrom sigue sin volcar y **no es alcanzable por esta vía**.
- El chip ID en `0x3D100000` (`CHIPID_BASE` de Rockbox) devuelve ceros.
  INFERIDO: el bloque está con el reloj cortado. Encenderlo sería escribir en
  un registro de clock gate — volátil, pero es una escritura, así que no lo hice.
- Cargar `osos_decrypted.bin` en la máquina `ipod-classic` de QEMU. Esto es lo
  que desbloquea el objetivo original del proyecto.

## Fuentes

- [EmCORE Monitor Protocol](https://freemyipod.org/wiki/EmCORE_Monitor_Protocol)
- [EmCORE Releases](https://freemyipod.org/wiki/EmCORE_Releases)
- [github.com/freemyipod/freemyipod](https://github.com/freemyipod/freemyipod) — espejo del SVN. r859 = commit `92b156a3b52f73f39153c1b24177b28e3c910cdd`
- `assets/rockbox/firmware/target/arm/s5l8702/crypto-s5l8702.{c,h}`
- `assets/ref/giek/DECRYPTION_GUIDE.md`
