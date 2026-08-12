# Los muros del iPod classic 6G, y cómo cayó cada uno

Crónica de síntesis del puerto del **iPod classic 6G** (SoC Samsung S5L8702) a
QEMU: de un firmware cifrado ilegible a un iPod que arranca el retailOS original
de Apple, dibuja su interfaz, se navega, y **lista música**. Cada entrada es un
obstáculo real: el **síntoma** con que se manifestó, la **causa** de fondo, y
**cómo** se desbloqueó. Las direcciones son VA del firmware (base = offset en
`osos_decrypted.bin` + `0x07FF51B8`).

El detalle técnico de cada uno está en los documentos que se citan; esto es el
mapa.

---

## 1. El firmware estaba cifrado — ni siquiera se podía leer

**Síntoma.** El `osos` del `.ipsw` medía entropía 7,99 bits/byte, cero cadenas
ASCII. Ilegible. ([01](01-firmware-ipsw.md))

**Causa.** Cifrado AES con la clave GID, que vive dentro del silicio del
S5L8702 y no se puede extraer.

**Cómo cayó.** No hacía falta *tener* la clave: bastaba con que **descifrara el
propio aparato**. Con el iPod físico del usuario corriendo emCORE, su motor AES
descifró el payload por USB en 1,2 s. Detalle fino que costó: `hwkeyaes()` pone
el IV a cero al principio de cada llamada, así que trocear rompía el primer
bloque de cada trozo; se repara con un XOR contra el último bloque cifrado
anterior. Resultado: entropía 5,76, 42.014 cadenas, `Copyright 2001-2008 Apple
Inc.`. ([10](10-classic6g-emcore.md))

---

## 2. Arrancaba, pero sin disco se mataba solo

**Síntoma.** El retailOS llegaba a su bucle de reposo RTXC y entraba en un ciclo
de caída: 19 Data Aborts + 19 reinicios por software en 20 s, invisibles porque
salían por **stderr** vía semihosting (`CPU_exception_handler`).

**Causa.** Una llamada virtual de C++ sobre un objeto **nulo**. El nulo venía de
una búsqueda de recursos `"FONT"`/`"Str "` que devolvía NULL, y el firmware no
lo comprobaba. Sin fuentes, no hay interfaz, y el sistema muere.

**Cómo cayó.** Adjuntar un disco. Las fuentes viven en el disco, no en el
firmware. ([11](11-classic6g-retailos.md) §4)

---

## 3. Pantalla negra pese a llegar al bucle principal

**Síntoma.** El sistema corría limpio (miles de IRQ, cero excepciones) pero no
dibujaba nada.

**Causa (primera capa).** Un modelo de RNG en `0x3C100000` gateaba la lectura
tras un bit de "enable" que nunca se escribía con valor no nulo → bloqueo en el
primer sorteo, un bucle de 48 millones de iteraciones.

**Cómo cayó.** Quitar la puerta falsa. El comentario heredado decía que
`0x08031db0` escribía 1 al enable; el literal de ahí era `0x08913CFC`, un flag
en bss. Se verificó desensamblando, no leyendo el comentario.
([11](11-classic6g-retailos.md) §19.2)

---

## 4. El muro grande de la imagen: el compositor de vídeo

**Síntoma.** El firmware emitía comandos de LCD pero ni un píxel llegaba al
panel. Un agente llegó a descartar `0x38900000` porque *"ningún registro recibe
algo con pinta de dirección de DRAM"*.

**Causa.** `0x38900000` **es el compositor de vídeo**, no un LCD simple. La
imagen se compone por capas en DRAM y un motor la empuja al panel — por eso
ninguna escritura de píxel pasaba por el bus. El descarte era falso: se
escribían 793 direcciones distintas, entre ellas una **tabla de gamma
identidad** (768 palabras = 3×256) y la **geometría del panel al píxel**
(`+0x214 = 0x013F00EF` = 319×239).

**Cómo cayó.** Modelar el compositor (`ipod_classic_disp.c`): cinco descriptores
de capa, y volcar el framebuffer de DRAM con `pmemsave` reveló **la pantalla de
idiomas entera ya compuesta**. No había que inventar píxeles, había que
copiarlos al panel. ([11](11-classic6g-retailos.md) §13-14)

> **Lección.** Contar accesos a un bloque sin mirar *qué valores* lleva es cómo
> se descartó por error la pieza más importante del proyecto.

---

## 5. El `memcpy` por DMA que nunca volvía

**Síntoma.** Con disco, la interfaz esperaba para siempre. El diagnóstico
apuntaba al ATA.

**Causa.** El ATA era **inocente**: su ciclo se cerraba y su semáforo despertaba.
Quien colgaba era un `memcpy` **posterior** por DMAC0 (un PL080) que dormía sin
timeout esperando la IRQ de fin de cuenta.

**Cómo cayó.** Cablear los dos PL080 de QEMU (`hw/dma/pl080.c`) con la memoria
del sistema como puerto maestro. ([11](11-classic6g-retailos.md) §14)

---

## 6. Las fuentes "que no estaban"

**Síntoma.** El cargador iba a `Resources/Fonts` y no encontraba nada. Nuestra
propia wiki daba la imagen `rsrc` del `.ipsw` por "rellena de `0xFF`, se genera
en el aparato".

**Causa.** Falso. La `rsrc` **es un volumen FAT16 real** llamado `IPODRESOURC`,
con Helvetica y las fuentes CJK dentro. Se leía como vacía porque un vistazo sin
el **sesgo de `0x1000`** del contenedor cae en el relleno; el volumen empieza en
`0x7e00`.

**Cómo cayó.** Aplicar el sesgo y montar ese volumen. De ahí salen los kanji y
el griego de la pantalla de idiomas. ([01](01-firmware-ipsw.md))

---

## 7. El compositor tapaba la interfaz

**Síntoma.** Las teclas funcionaban pero la pantalla no cambiaba: *lo que hacían
no se veía*.

**Causa.** `disp_draw_layer()` dibujaba toda capa con dirección de origen. El
firmware, al terminar una transición, apaga la capa pero **deja el descriptor
puesto**. La capa 2 se quedaba encima de la 0 para siempre, tapando el menú, que
estaba dibujado debajo todo el tiempo.

**Cómo cayó.** Honrar los bits de `CONTROL` (`+0x008`): bit 6 → capa 0, bit 5 →
capa 1, bit 4 → capa 2. Se destapó volcando el búfer de la capa 0 y **mirándolo**
— el menú entero estaba ahí. ([11](11-classic6g-retailos.md) §18)

---

## 8. Las teclas: la rueda de clic y la puerta del PMU

**Síntoma.** Primero, nada de entrada. Después, el giro funcionaba y **los
botones no**.

**Causa (dos capas).** (a) La rueda de clic en `0x3C200000` no estaba modelada;
y `WHEELMASK` no devolvía lo escrito, así que el firmware se enmascaraba a sí
mismo. (b) Con la rueda puesta, los botones caían en una **puerta silenciosa**:
21 instrucciones dentro del despachador de teclas, el firmware lee el registro
`0x87` del PMU (interruptor de bloqueo) y vuelve si dice "bloqueado". Nuestro
PMU lo devolvía a cero.

**Cómo cayó.** Modelar la rueda (IRQ 23, lectura honrada de `WHEELMASK`, paquetes
espaciados por temporizador) y reportar el interruptor de bloqueo como *abierto*.
([11](11-classic6g-retailos.md) §17)

> **Lección, afinada dos veces.** Que el modelo levante una línea no significa
> que el firmware la reciba. Hay que comprobar el camino entero — y *para cada
> tipo de evento*: el giro y los botones seguían caminos distintos.

---

## 9. "MENU no vuelve atrás" — el muro que no existía

**Síntoma.** En la pantalla de idiomas, MENU no hacía nada (0 px).

**Causa.** Ninguna. En esa pantalla **Apple dejó la regla vacía**: es la primera
de la secuencia de bienvenida y no hay pila a la que volver. De 108 reglas
`button.menu.up`, 62 hacen algo y 46 no. Desde una pantalla más profunda, MENU
funciona — lo confirmó el usuario navegando.

**Cómo cayó.** Reconociendo que el modelo hacía lo correcto. La medida "0 px" era
válida; la conclusión "hay un bug" era el error. ([11](11-classic6g-retailos.md) §18.5)

---

## 10. La música (a): el volumen FAT32 se rechazaba al montar

**Síntoma.** "No Music" aunque el disco llevaba la biblioteca. El firmware leía
la cabecera del volumen **una sola vez** y no volvía a tocarlo — ni directorio,
ni `iPod_Control`.

**Causa.** El clasificador de volúmenes ve el `0xAA55` que **todo sector de
arranque FAT lleva** en `+0x1FE` y lo trata como MBR anidado. Al no hallar
entradas de partición, cae a un último recurso: `memcmp(boot+0x34, "FAT", 3)`.
`mkfs.vfat` deja esos bytes a cero (el `"FAT32   "` real está en `+0x52`, que ese
camino no mira) → rechazo.

**Cómo cayó.** Escribir `"FAT"` en `boot+0x34`, un campo reservado que ningún
driver FAT real lee (verificado contra Rockbox). A/B aislado: con el marcador, 85
lecturas dentro de la partición; sin él, 3. Antes se descartaron con medidas el
tamaño de sector (512/2048/4096), el tipo de partición (0x0B/0x0C) y el número de
clusters. ([13](13-classic6g-musica.md) §5-6)

---

## 11. La música (b): la firma del `iTunesDB` eran veinte ceros

**Síntoma.** El volumen montaba, el firmware parseaba el `iTunesDB`, pero la
biblioteca salía vacía: fallaba la validación de la **firma** (`hash58`,
`mhbd+0x58`). Forzar el GUID FireWire correcto no bastaba.

**Causa.** La firma que este firmware espera son **20 bytes a cero**. El
algoritmo es `itdb_hash58` de libgpod byte a byte, pero la SHA-1 del S5L8702 es
un **acelerador por hardware en `0x38000000` que QEMU no modela**: sus lecturas
devuelven 0, el digest sale 0, la clave HMAC sale 0, y el hash esperado colapsa a
veinte ceros (medido en el `memcmp` `0x08328598`).

**Cómo cayó.** Poner a cero el `hash58` de cada copia del `iTunesDB`
(`mkdisk6g4k.py`, anotado como stub hasta que se modele el acelerador). A/B
decisivo: firma real de libgpod → biblioteca vacía; ceros → "1 曲". El firmware
**sí valida**, y exige ceros. ([13](13-classic6g-musica.md) §7-8)

> **Ironía.** Empezamos temiendo la firma, la descartamos por prematura cuando
> resultó ser el montaje, y al final la firma volvió a ser el muro — con una
> respuesta que nadie habría adivinado sin medirla.

---

## El patrón que se repitió: el descarte que era la pista

**Tres veces** en este proyecto algo dado por descartado resultó ser la clave:

| Descartado como… | Era en realidad… |
|---|---|
| `0x38900000`, "ningún registro con dirección de DRAM" | el compositor de vídeo, la pieza central |
| la imagen `rsrc`, "vacía, se genera en el aparato" | un FAT16 con las tipografías |
| el ATA, "culpable del cuelgue" | inocente; era un `memcpy` por DMA posterior |

Qué tienen en común: los tres se descartaron **contando o suponiendo**, no
mirando el valor concreto. La cura, repetida hasta ser doctrina: desensambla el
binario en vez de razonar sobre el hardware, mira *qué* dato lleva un registro y
no solo *cuántos* accesos hay, y trata cada medida como sospechosa hasta validar
el instrumento con un caso de control.

---

## Muros que siguen en pie

- **Que suene.** El camino de audio (I²S + DMA de audio + códec) no está
  modelado; hoy ~6.000 accesos por arranque se tiran al suelo. En curso — ver
  [14-classic6g-audio.md](14-classic6g-audio.md) cuando exista.
- **"Connect to Power" / reinicio a los ~46 s.** El modelo de energía hace creer
  al retailOS que la batería está vacía (lee la carga por un camino que no
  modelamos; el ADC que damos es el que quiere Rockbox, no Apple). Acotado.
- **La bootrom del S5L8702.** `0x20000000` devuelve cuatro constantes repetidas:
  está bloqueada tras el arranque. No hay volcado posible por emCORE; haría falta
  DFU + exploit. ([10](10-classic6g-emcore.md))
