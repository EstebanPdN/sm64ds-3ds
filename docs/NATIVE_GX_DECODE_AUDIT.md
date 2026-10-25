# Auditoría del decode GX inicial de DSMT

El rechazo de GX del intento integrado `1791596592223444000` se reproduce en
host y queda corregido en el decoder: el material original del cuerpo usa
texgen 2, que todavía se rechazaba. El replay con los uploads conservados
acepta las 1.466 entradas, emite 78 triángulos y sus 180 UV generadas coinciden
con la expresión entera del manual Nintendo. Es evidencia de decode en host;
no demuestra que el menú haya presentado un frame ni que se pueda empezar
una partida. La auditoría inicial del intento `1791595263336119000` permanece
documentada abajo.

Los fuentes originales revisados pertenecen al commit
`6230b2996636e3fd0e03cb178161c94cb4b73cc9`. La especificación primaria de
comandos, longitudes, matriz y formato de FIFO es
[GBATEK, capítulos DS 3D Geometry Commands y DS 3D Matrix Load/Multiply](https://raw.githubusercontent.com/mgba-emu/gbatek/master/ds.md).

## Bloque original y comandos admitidos

`func_ov007_020bcf90` prepara las cámaras, emite los sprites y llama
`func_ov007_020b7658 → func_02049ee8 → func_0204a5c8` para las partículas.
El binding `func_0204af3c` selecciona la textura y configura su matriz con
modo 3, identidad, escala de tres palabras y vuelta a modo 1. La matriz
`func_0205532c` usa FIFO `0x19` seguido de nueve palabras de rotación y tres
de traslación. Las direcciones `0x04000400…0x0400043f` son aliases de FIFO;
la copia de nueve palabras no puede tratar esos destinos como RAM.

| Familia del bloque revisado | IDs GX | Parámetros por comando |
|---|---|---|
| Selección y pila de matriz | `10`, `11`, `12`, `13`, `14`, `15` | 1, 0, 1, 1, 1, 0 |
| Carga y multiplicación | `16`, `17`, `18`, `19`, `1a` | 16, 12, 16, 12, 9 |
| Escala y traslación | `1b`, `1c` | 3, 3 |
| Color, normal y UV | `20`, `21`, `22` | 1, 1, 1 |
| Vértice y componentes | `23`, `24`, `25`, `26`, `27`, `28` | 2, 1, 1, 1, 1, 1 |
| Atributos y binding | `29`, `2a`, `2b` | 1, 1, 1 |
| Material y luces | `30`, `31`, `32`, `33` | 1, 1, 1, 1 |
| Inicio/final, swap, viewport | `40`, `41`, `50`, `60` | 1, 0, 1, 1 |

Todos tienen longitud y caso de ejecución en `source/vendor/ntr/gx.cpp`.
Esto descarta la ausencia de esos IDs como explicación del rechazo; no
prueba que el stream integrado haya recibido todos sus parámetros. El
snapshot previo al decode permite resolver esa diferencia.

Los helpers de quads `func_0204c194` y `func_0204c24c` usan `VTX_10`. Los
sprites `func_ov007_020c4684` y su emisor por tiles `020c49bc` usan pares de
`VTX_16` y comandos parciales `VTX_XZ`/`VTX_YZ`. Sus secuencias no necesitan
los tests de geometría `70`–`72`, ni texgen 2 o 3. Las rutas de otros estados,
display lists de modelos y callbacks indirectos necesitan sus propios datos;
esta lista no es una prueba exhaustiva de todos los frames del menú.

## Binding de partículas contrastado

La auditoría acotada de datos, `build/menu-data-audit/render-textures.json`,
compara cinco etapas del parser, uploads, sistemas y binding entre ROM y
ARM11. Sus 16 escrituras GX coinciden. Usa el recurso auténtico
`data/DSMT/mdst_kira1_spa.bin`, handle 658, 320 bytes, SHA256
`6407bacff4838b43343468ad7507a5735e33dda8e0cb9c74edf4bcdeeba82596`.

El primer binding observado es `TEXIMAGE=0x689072b0`, `PLTT=0xb8`: formato 2,
texgen 1, 16×16, texels desde `0x39580`, 64 bytes, y paleta desde `0x5c0`,
8 bytes. La segunda textura del recurso se sube a `0x39540` y su paleta a
`0x5c8`. Los offsets caben en las capacidades de la configuración original
`Tex=3` (256 KiB) y `TexPltt=0x20` (16 KiB). Ambos sistemas empiezan usando
la definición 0 y por eso repiten el primer binding.

El reporte de datos declara sus seams: allocator con canarios, archivo real,
observador de upload SDK y observación de escrituras GX. El cursor anterior
de modelos es una fixture derivada del binding observado, no una prueba de
todas las asignaciones anteriores. Los bytes y flags conservados del intento
integrado deben reproducir la resolución efectiva del frame.

La prueba complementaria de host
`build/native-runtime/menu-texture-binding-test.json` usa las dos texturas y
paletas SPA auténticas con `GameGeometry`, `GameTextures` y `texture_decode`.
ASan/UBSan pasa, ambas resuelven 16×16 y sus 256 píxeles coinciden con una
referencia 2bpp independiente; cada textura tiene 21 píxeles opacos. Eso
descarta un defecto de resolución aislado en estos dos recursos. Aún no
equivale al replay de todas las texturas del intento integrado.

## Reproducción sin otro emulador

`tools/audit_native_gx_stream.py` compila y ejecuta `tools/native_gx_stream_probe.cpp`
con el decoder y resolver de producción. Lee los cuatro snapshots conservados
antes del decode:

- `scene-1-first-gx.bin`: entradas `DrawWord` de 8 bytes, little endian.
- `scene-1-first-textures.bin`: los 524.288 bytes de almacenamiento poseído.
- `scene-1-first-palettes.bin`: los 98.304 bytes de almacenamiento poseído.
- `scene-1-first-geometry.json`: capacidades, generaciones, contadores y flags
  exactos publicados por `GameGeometry`.

Ejemplo:

```sh
python3 tools/audit_native_gx_stream.py \
  --snapshot-dir build/native-runtime-azahar/attempt-<id> \
  --report build/native-gx-audit/attempt-<id>.json
```

El replay real utiliza `GameTextures::resolve` con los uploads conservados.
En una ejecución separada, una fixture explícita de textura aísla únicamente
la estructura de comandos. Ninguna ejecución rasteriza una imagen. La fixture
nunca reemplaza bytes faltantes en el replay real ni se cuenta como frame del
juego. El estado inicial de matrices del probe es identidad; la prueba sirve
para longitud/aceptación/bindings, no para reproducir la presentación completa
ni el estado matricial heredado de frames anteriores.

El reporte guarda los hashes de entradas y fuentes, las consultas de textura,
el índice de la primera causa explícita y los parámetros pendientes al final.
Consume `gx_get_stream_diagnostics()` sin modificar el decoder. Los controles
ASan/UBSan pasan 16 casos: textura indexada sintética con dirección del binding
observado, triángulo sin textura, matriz completa/incompleta, `VTX_16`
incompleto, texgen 2 admitido, texgen 3 rechazado, PUSH/POP de textura y sus
índices no cubiertos rechazados, comando desconocido, `BOX_TEST` sin ejecución,
primera causa retenida, paleta fuera de capacidad y fixture de protocolo separada.
Cada caso comprueba también que `gx_reset` limpia el fallo y las colas.

## Texgen 2 del material original, corregido con precisión entera

El recurso auténtico `data/DSMT/face_demo_mariostar.bmd`, handle 649, contiene
el material `mat_body` con flags `0x80030000` y la textura `star_body04` con
flags `0x15200000`. Su combinación produce el binding capturado
`TEXIMAGE=0x95230000`, `PLTT=0x3b0`, formato comprimido 5, 32×32, texgen 2.
El reporte `build/native-runtime/menu-star-material-audit.json` compara
el productor ROM y ARM11: 345 palabras GX idénticas, uploads byte por byte y
display list original completo. Después de `func_02055998` quedan modo 3,
`LOAD_4x4` y `STORE 0`; no falta un cambio de modo 1 en el port.

La referencia primaria es el
[Nintendo NITRO Programming Manual V1.44, NTR-06-0180-001-A9, pp. 213–214](https://www.scribd.com/document/52151943/NDS-NITRO-Programming-Manual-V1-44).
La entrada es la normal original de `NORMAL`, antes de la matriz direccional.
La generación ocurre al ejecutar ese comando. S/T del `TEXCOORD` anterior
reemplazan la traslación de la matriz. La expresión en unidades originales
de 1/16 de texel equivale a:

```text
Sraw' = Sraw + floor((Nx*M[0] + Ny*M[4] + Nz*M[8]) / 2^21)
Traw' = Traw + floor((Nx*M[1] + Ny*M[5] + Nz*M[9]) / 2^21)
```

Se conservan los 16 bits del resultado firmado y después se convierten a
texels. Usar `>>17` daría otra escala; el manual explica explícitamente la
unidad de 1/16 de texel. El decoder mantiene una matriz de textura entera
paralela a la existente en float, conserva las cargas originales y actualiza
identidad/multiplicación/escala/traslación con un único descarte de 12 bits
fraccionales por producto matricial. Las sumas modulares evitan overflow
firmado de C++. Esta precisión no depende de reconstruir enteros de floats.
El cálculo de luz queda separado de la generación de UV.

`tools/test_native_texgen.py` ejecuta 442 casos bajo ASan/UBSan y compara
1.134 UV emitidas contra una referencia independiente con enteros Python sin
límite, escrita en la forma `<<3`, `<<12`, `>>24` del manual. Cubre signos,
fracciones, wrap, normales sin normalizar, una matriz direccional diferente,
cargas 4×4/4×3, productos 4×4/4×3/3×3, escalas, traslación, secuencia de
comandos y las dos matrices guardadas. Una copia del decoder previo falla
441 casos; una segunda copia que solo retira el rechazo falla esos mismos
441, incluida la comparación de UV del material y la conservación del slot
de posición. El reporte es `build/native-gx-audit/texgen2-test.json`.

El replay real completo está en
`build/native-gx-audit/attempt-1791596592223444000-texgen2.json`, con hashes
de los cuatro inputs. Ambos bindings consumidos resuelven sus bytes originales,
ninguna cola queda pendiente y las UV de los triángulos 6–65 coinciden
exactamente: 60 triángulos, 180 vértices. Las coordenadas del primer triángulo
del cuerpo son `(-3.1875,35.0625)`, `(0.5,28.875)` y `(6,34.375)`; sus bits
repeat originales permiten coordenadas fuera del intervalo de textura.
La compilación aislada de `gx.cpp` para ARM11 también pasa con devkitARM,
`-march=armv6k -marm -mfpu=vfp -mfloat-abi=hard -Wall -Wextra -Werror`.
Esta comprobación de compilación no es una ejecución del decoder en ARM11.

## Matriz de textura separada y proyección por triángulo

El decoder previo encaminaba los comandos de pila de modo 3 a las matrices
de posición/vector. Ahora el `STORE/RESTORE 0` observado guarda y recupera la
matriz de textura float y su copia entera sin tocar los slots de huesos ni
las profundidades de posición/proyección. La regresión verifica una posición
guardada distinta de la vigente al guardar textura y compara las posiciones
emitidas después de restaurar ambas matrices. Se mantiene el contrato acotado
del menú: otros índices de textura, PUSH/POP de modo 3 y RESTORE 0 sin STORE
previo fallan explícitamente como `matrix_operation`, valor 4. Los motivos
existentes conservan sus valores 0–3. No se inventa profundidad de textura
en `GxStackState` ni en las consultas SDK nativas. GBATEK deja incertidumbre
en la profundidad de esa pila; no se declara una emulación completa de ella.

Cada triángulo emitido conserva además la proyección original vigente en
`GxTriangle::projection[16]` y `native_projection=true`, para que la presentación
nativa no aplique una matriz posterior a los triángulos anteriores. La
auditoría independiente `build/native-runtime/gx-projection-audit.json`
identificó 6 triángulos con una proyección y 72 con otra, con cero triángulos
que mezclen proyecciones entre sus vértices en este stream. Esto respalda
el contrato por triángulo en la captura observada; no prueba streams futuros
que cambien la proyección dentro de una primitiva. La selección y adaptación
de proyecciones del renderer tienen sus propias pruebas.

## Iluminación corregida y otros límites separados del rechazo

La revisión encontró que `POLYGON_ATTR` copiaba solo alpha, mientras que
`NORMAL` dependía de `g.light_mask` y los bits 0–3 no la actualizaban. El
decoder ahora toma esos bits del comando original. Esto afecta al color de
modelos con normales; el defecto no provocaba por sí mismo un stream inválido.

`tests/test_game_draw.cpp` configura por puertos dos luces direccionales roja
y verde, un material blanco y la normal del triángulo. Las máscaras 0/1/2/3
deben producir negro/rojo/verde/amarillo en los 12 vértices resultantes.
`tools/test_native_light_mask.py` pasa toda la regresión bajo ASan/UBSan y
demuestra que una copia temporal retirando solo la asignación de máscara
falla exactamente la comparación de color. El reporte está en
`build/native-gx-audit/light-mask-test.json`. La comprobación es de color de
vértice de host, no de un frame del menú ni de fidelidad completa de iluminación.
Toon, specular y otras propiedades de polígono conservan límites adicionales;
conservar las escrituras no prueba esos efectos.

Los comandos `BOX_TEST`, `POS_TEST`, `VEC_TEST` tienen longitud reconocida pero
no implementación de ejecución. Texgen 3 y las operaciones de textura fuera
del contrato anterior siguen fallando explícitamente. Estas pruebas no
acreditan fidelidad completa del rasterizado, clipping, iluminación o polígonos.

La ejecución del inspector es evidencia de host. Los oráculos de hojas son
evidencia acotada de CPU/ABI ROM y ARM11. El intento integrado pertenece a
Azahar con perfil Old 3DS. La consola física, velocidad correcta y 60 FPS
estables siguen sin verificar.
