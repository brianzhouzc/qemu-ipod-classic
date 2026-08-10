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

### La imagen `8702` de `0x8000`: descifrada, y **no es de Apple**

Cae en el hueco entre `umsboot` (acaba en `0x7362`) y `emcore` (`0xA000`), y
**no está en el directorio de emCORE**. La cabecera es el mismo contenedor
`8702`/`1.0` que el `osos` del `.ipsw`, pero de tipo `0x01` en vez de `0x03`:

```
@0x008000  magic='8702' ver='1.0' enc_type=0x01  entry=0x00000000  data_sz=0xE10 (3600)
```

`0x8000` no es un sitio cualquiera: es `NORBOOT_OFF`, la ranura de arranque de
la NOR (`assets/ref/firmware__target__arm__s5l8702__norboot-target.h:81`), y el
mapa en comentario de ese mismo fichero (líneas 30-70) la rotula
*"Original NOR Boot"*, con cabecera IM3 en `+0x000` y cuerpo en `+0x800`.

> **La primera versión de esta página decía, marcado como INFERIDO, que era el
> cargador NOR original de Apple. Es FALSO.** Está descifrado y dice
> literalmente `emCORE Loader v0.1.0 r859`. La inferencia era razonable y era
> incorrecta; lo que la mató fue medir en vez de razonar.

#### La cabecera estaba mal parseada — `fwtool.py` sólo vale para `enc_type` 3/4

La estructura buena es `struct Im3Info`, en
`assets/ref/firmware__target__arm__s5l8702__crypto-s5l8702.h:50-70`:

| Offset | Campo | En esta imagen |
|---|---|---|
| `0x00` | `ident[4]` | `"8702"` |
| `0x04` | `version[3]` | `"1.0"` |
| `0x07` | `enc_type` | `0x01` |
| `0x08` | `entry[4]` LE | `0x00000000` |
| `0x0C` | `data_sz[4]` LE | `0xE10` |
| `0x10` | unión: `enc_type` 1/2 → `data_sign[16]` + 32 B reservados;<br>`enc_type` 3/4 → `sign_off`, `cert_off`, `cert_sz` + 36 B | `39d2c14f a34171ce 424a6a0b e2b9b51c` |
| `0x40` | `info_sign[16]` | `db239026 2759c80a 6a8330d3 2bd33b61` |

`tools/ipod_classic/fwtool.py:67-107` implementa **sólo** la variante 3/4 y
llama `unknown_08` a lo que es el punto de entrada. Sobre el `osos` acierta
porque el `osos` es `enc_type=0x03`; sobre esta imagen daba
`body_size=0x4fc1d239`, que es basura — eran los primeros cuatro bytes de la
firma. Herramienta nueva y correcta: `tools/ipod_classic/norboot_im3.py`.

#### Qué clave: no se adivina, se comprueba — VERIFICADO

`im3_sign` (`crypto-s5l8702.c:117-123`) es
`AES_encrypt(keyidx, SHA1(data)[0:16])`, AES-128-CBC con IV = 0. Eso es un
**predicado comprobable**: calculamos el SHA-1 en el PC, le pedimos al aparato
que cifre los 16 bytes con cada índice de clave, y comparamos con la firma
guardada. No hay que probar a ver qué "parece código".

| Qué se firma | Clave 1 (GKEY/GID) | Clave 2 (UKEY, única del aparato) |
|---|---|---|
| `Im3Info[0:0x40]` → `info_sign` | `309eb6cf…` no | `db239026…` **coincide** |
| texto claro del cuerpo → `data_sign` | `28cc742d…` no | `39d2c14f…` **coincide** |

O sea: descifrar con la clave 2 produce un texto claro cuyo SHA-1 truncado,
cifrado por el motor del propio aparato, reproduce **byte a byte** los 16 bytes
que Apple/emCORE dejó en `+0x10`. Eso no es un juicio estético sobre un
desensamblado: es una comprobación criptográfica.

Y de paso identifica al autor: **UKEY es única por aparato**, así que una
imagen firmada con ella no puede venir de fábrica. La escribió el instalador de
emCORE en este iPod concreto. Es exactamente lo que hace `im3_crypt`
(`crypto-s5l8702.c:126-133`), cuyo comentario dice *"only supports enc_type 1
and 2 (UKEY)"*.

```bash
python3 tools/ipod_classic/norboot_im3.py \
        assets/classic6g/bootflash_nor_1MB.bin assets/classic6g 0x8000
```

| Fichero | Tamaño | SHA-1 |
|---|---|---|
| `assets/classic6g/norboot_emcoreldr_decrypted.bin` | 3 600 | `130b9b21f9b2531de01966d259622d6bfb047599` |

Entropía 7,948 → **6,458**; 54,9 % de palabras con `cond = 0xE`; y las cadenas
`emCORE Loader v0.1.0 r859`, `Loading emCORE...`, `File not found!`, más el
nombre de fichero de 8 caracteres `emcore  ` que busca en el directorio de
`0x1000`.

#### El cargador NOR de Apple ya NO ESTÁ en este aparato — hallazgo negativo

`assets/ref/giek/architecture_BOOT_CHAIN.md` dice que hay copia primaria en
`0x08000` y **copia de respaldo en `0x22000`**. Medido sobre el volcado real:
en el megabyte entero de NOR hay **una sola** aparición de la firma `8702`, la
de `0x8000`. `0x22000` cae dentro de `bootmenu` (`0x18000`+`0x24091`), o sea
que el instalador de emCORE lo pisó. No hay ONB que recuperar por esta vía.

Tampoco está en el `.ipsw`: `aupd_decrypted.bin` y `osos_decrypted.bin` no
contienen ninguna cabecera `8702` (buscadas: 1 aparición en `aupd`, que no
lleva `"1.0"` detrás, y 0 en `osos`).

#### Qué hace el cargador, instrucción a instrucción

El bootrom carga el cuerpo IM3 en `IRAM0_ORIG = 0x22000000` y salta ahí
(`assets/ref/bootloader__ipod-s5l87xx.c:306-318`), lo que concuerda con
`entry = 0`. Desensamblado con esa base:

```bash
arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x22000000 \
    assets/classic6g/norboot_emcoreldr_decrypted.bin
```

Es ensamblador escrito a mano y muy comprimido: carga sus constantes con
`add sp, pc, #N` seguido de `pop {…}`, no con `ldr` de pool. La primera tabla
está en `+0x23C` y la segunda en `+0x260`.

**`+0x00`–`+0x34`, lo que el bootrom deja hecho.** El cargador empieza por
`msr CPSR_c, #0xD3` (SVC, IRQ y FIQ enmascaradas), `mrc`/`bic #1`/`mcr` sobre
CP15 c1 — es decir **apaga la MMU** —, invalida ICache (`c7,c5,0`) y TLB
(`c8,c7,0`), pone FCSE PID a 0 (`c13,c0,0`) y hace el bucle
`mrc p15,0,pc,c7,c14,3` (*test-clean-and-invalidate* de DCache) hasta que sale
`Z`. INFERIDO, y con la fuerza que tenga el código defensivo: el bootrom le
entrega el control **con la MMU encendida y las cachés sucias**.

**`+0x38`–`+0x64`, la MMU propia.** `fp = 0x2203C000` es la tabla L1 (16 KiB,
alineada); DACR = `0xFFFFFFFF` (todos los dominios en *manager*); el descriptor
inicial es `ip = 0x00000C1E` = sección, `AP = 11`, dominio 0, `C = 1`, `B = 1`.
El bucle escribe una sección por MiB en identidad, y en `0x38000000`
(`biccs ip, ip, #12`) apaga `C` y `B` — o sea **mapa plano 1:1 de
`0x00000000`–`0x3FFFFFFF`, cacheable abajo y sin caché de `0x38000000`
arriba**. Se para al llegar a `0x40000000` (`tst ip, #0x40000000`). Luego
`mcr p15,0,sl,c1,c0,0` con `sl = 0x0005107D`: MMU on, DCache on, *write
buffer* on, ICache on, alineación off, *little-endian*.

Detalle con trampa: sólo escribe 1024 entradas (4 KiB) de una tabla que ocupa
16 KiB, y acto seguido usa `0x2203D000` — dentro de la misma tabla — para otra
cosa. Las entradas de VA ≥ `0x40000000` quedan sin inicializar y luego
machacadas. Es deliberado y funciona porque nada mira ahí arriba.

**`+0x68`–`+0x8C`, y aquí está lo interesante: los píxeles.** El bucle escribe
grupos de cuatro palabras con `stmia fp!, {r6, r7, r8, r9}` — que es
exactamente un LLI de PL080 `{src, dst, next, control}` — con:

| Registro | Valor | Qué es |
|---|---|---|
| `r6` | `0x22010000` | origen: framebuffer en IRAM; avanza `r4 << 1` → 16 bits por dato |
| `r7` | `0x38300040` | destino fijo: **`LCD_WDATA`** |
| `r8` | `0x2203D000` | siguiente LLI; `+16` cada vuelta; `0` en el último |
| `r9` | `0x74240000` | palabra de control, con el tamaño metido por `orr` |
| `r4` | `0xFFF` | máximo por LLI |
| `r5` | `0x12C00` | **76 800 datos = 320 × 240** |

`0x38300040` es `LCD_WDATA` por `assets/ref/s5l87xx.h:1486`
(`LCD_BASE + 0x40`, con `LCD_BASE = 0x38300000` en la línea 1473). Y no hay que
fiarse sólo de eso: `assets/ref/firmware__target__arm__s5l8702__dma-s5l8702.h:86`
define `S5L8702_DADDR_PERI_LCD_WR (LCD_BASE + 0x40)`, es decir, esa misma
dirección **es la que Rockbox usa como destino de DMA para el LCD**, y la línea
43 le asigna la línea de petición `S5L8702_DMAC0_PERI_LCD_WR = 0x3`.

La palabra de control `0x74240000` se descompone así (PL080 `DMACCxControl`):
`I = 0`, `Prot = 7`, `DI = 0` (destino **sin** incrementar → es un FIFO),
`SI = 1` (origen sí), `DWidth = 001` = 16 bits, `SWidth = 001` = 16 bits,
ráfagas de 1, y los bits 11:0 (`TransferSize`) los pone el `orr r9, r9, r4`.

76 800 datos de 16 bits = 153 600 bytes = **un fotograma RGB565 de 320 × 240**,
que es la resolución del panel del classic 6G. 76 800 / 4095 → 19 LLIs.

> **VERIFICADO: en el S5L8702 el framebuffer sale por DMA PL080 hacia
> `LCD_WDATA`, no por escrituras de CPU.** Esto responde, por lo menos para
> emCORE, la pregunta que [11-classic6g-retailos.md](11-classic6g-retailos.md)
> §3 dejaba abierta como `INDETERMINADO` ("por dónde pasan los píxeles"), y
> asciende el candidato nº 4 de su §6 —*DMAC0, que se programa con descriptores
> en DRAM y nunca completa*— de "por descartar" a **sospechoso principal**.
> Que el retailOS use la misma ruta es `INFERIDO`, no medido; ver abajo.

**`+0x90`–`+0x158`, relojes y temporizador.** Un segundo `pop` de 13 registros
desde `+0x260` deja `r0 = 0x3C500000`, que es `CLK_BASE` (`s5l87xx.h:147`).
Escribe `+0x48 = 0xFDFFFFE5` y `+0x4C = 0xFFFFEFC9` (compuertas de reloj),
cero en `+0x00`, `+0x04` y `+0x60`, `+0x28 = 0x01002401`, `+0x38 = 0x00007E90`,
`+0x44 = 0x00040034`, y entonces **gira esperando el bit 2 de `+0x40`** —
enganche del PLL — antes de poner `+0x04 = 0x00404101` y `+0x00 = 0x3000`.
Después `+0x08` y `+0x0C = 0x80008000`, `+0x10` y `+0x14 = 0x8000`.
`r6 = 0x38501000` (`SM1_BASE + 0x1000`, `s5l87xx.h:514`) recibe un `3`.
`r1 = r0 | 0x200000 = 0x3C700000` es `TIMER_BASE`: `+0xA0 = 0x440`,
`+0xB0 = 0x0B`, `+0xA8 = 0xFFFFFFFF`, `+0xA4 = 3`.

> Para la máquina de QEMU esto es un contrato explícito: **el bit 2 de
> `CLK_BASE + 0x40` tiene que subir solo**, o cualquier firmware que arranque
> por esta ruta se queda girando. Es el patrón 1 de
> [09-historia-5g.md](09-historia-5g.md).

**`+0x118`–`+0x158`, GPIO.** `r3 = 0x3CF00000` = `GPIO_BASE` (`s5l87xx.h:1370`).
Un bucle de 16 vueltas con paso de 32 bytes (`PCON(i)` está en
`GPIO_BASE + (i << 5)`, `s5l87xx.h:1378-1381`) escribe una constante en `+0x00`
(`PCON`) y ceros en `+0x0C` (`PUNB`) y `+0x10`, hasta que la dirección alcanza
`0x…200` = 16 × 32.

El resto (≈ `+0x160` en adelante) es el cuerpo del cargador: rutinas
`bl 0x22000980` y `bl 0x220009A0` que parecen la interfaz de I²C con el PMU
(constantes `0x16`, `0x0C`, `0x19`, `0x4B` como registros), la fuente de 5 × 8
píxeles en `+0x5C0`–`+0x790` y los tres mensajes de texto en `+0xDCC`.
`INDETERMINADO` en detalle; no lo he trazado entero.

#### Un resto sin identificar en `0x9610`

Justo detrás del cuerpo IM3 (`0x8800 + 0xE10 = 0x9610`) y hasta `0x9E10` hay
`0x800` bytes exactos que **no están en el directorio de emCORE** y empiezan
por la firma `emCO` + `exec`, que es el formato de imagen de emCORE:

```
4f436d65 63657865 00000001 00000030 000326a0 00000e80
00001000 00000000 000326d0 00000145 00000001 4f020571
   emCO     exec     ver     hdr_sz
```

Guardado en `assets/classic6g/norboot_trailer_emco_0x9610.bin`
(SHA-1 `7d5e37b50dbd8287491e58544401eef3ecc89a7b`). Descifrarlo con GKEY y con
UKEY deja entropía 7,90 y 7,91 y ~5 % de `cond=0xE`, o sea **ruido con las dos**:
no está cifrado con ninguna clave fusionada. INFERIDO: es carga útil comprimida
(emCORE comprime sus imágenes), no cifrada. Qué es y para qué:
`INDETERMINADO`.

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
| `norboot_emcoreldr_decrypted.bin` | 3 600 | **el cargador NOR en claro** (emCORE Loader r859) |
| `norboot_trailer_emco_0x9610.bin` | 2 048 | el resto `emCOexec` sin identificar |

Pendiente:

- ~~Descifrar la imagen `8702` de `0x8000`~~ **hecho** (§3). Resultado: es el
  cargador de emCORE, no el de Apple.
- **Medir si el retailOS programa DMAC0 hacia `LCD_WDATA`.** Es la continuación
  directa del hallazgo del §3 y no necesita el aparato. `0x38200000` (DMAC0)
  hoy es un simple *stub* de registro en `hw/arm/ipod_classic/ipod_classic.c:259`,
  así que basta con:
  ```bash
  ./qemu-system-arm -M iPod-Classic -kernel ../assets/classic6g/osos_decrypted.bin \
      -display none -monitor none -serial null -drive if=ide,format=raw,file=/tmp/ipod.img \
      -d unimp,guest_errors -D /tmp/u.log
  grep -c 's5l8702.dmac0' /tmp/u.log
  ```
  La prueba de fuego: buscar una escritura de `0x38300040` en
  `DMAC0 + 0x104 + n*0x20` (el `DstAddr` del canal *n* de un PL080). Si aparece,
  el camino a los píxeles del retailOS está identificado. **No lo he medido yo**:
  otra sesión estaba recompilando el binario a la vez (`Text file busy`) y no
  quise pisarle el enlazado.
- El `.ipsw` no trae el cargador NOR de Apple y este aparato ya no lo tiene
  (§3). Si hace falta, hay que sacarlo de un iPod classic **sin** emCORE
  instalado, o por DFU tipo wInd3x.
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
- `assets/rockbox/firmware/target/arm/s5l8702/crypto-s5l8702.{c,h}` — `struct Im3Info`
  (`.h:50-70`), `im3_sign` y `im3_crypt` (`.c:117-133`)
- `assets/ref/firmware__target__arm__s5l8702__norboot-target.h:30-81` — el mapa
  de la NOR y `NORBOOT_OFF`
- `assets/ref/firmware__target__arm__s5l8702__dma-s5l8702.h:43,86` —
  `S5L8702_DMAC0_PERI_LCD_WR` y `S5L8702_DADDR_PERI_LCD_WR`
- `assets/ref/s5l87xx.h:147,514,1370-1381,1473,1486` — `CLK_BASE`, `SM1_BASE`,
  `GPIO_BASE`/`PCON`, `LCD_BASE`, `LCD_WDATA`
- `assets/ref/bootloader__ipod-s5l87xx.c:306-318` — qué hace el bootrom con la
  imagen IM3 de la NOR
- `assets/ref/giek/architecture_BOOT_CHAIN.md` — la copia de respaldo en
  `0x22000` que resultó no existir en este aparato
- `assets/ref/giek/DECRYPTION_GUIDE.md`
