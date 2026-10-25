# Datos originales para ARM11

Este módulo lee datos de la ROM del usuario a través de `AssetPack`. No ejecuta
código ARM9/ARM7 de DS, no convierte sus direcciones en punteros de la 3DS y no
contiene los bytes de las tablas del juego dentro del ejecutable.

## Implementado y comprobado

- Descompresión BLZ limitada por el tamaño de RAM del overlay; las referencias,
  cabeceras, relleno y límites se comprueban antes de publicar el resultado.
- Lectura de los 103 overlays ARM9 de la ROM ASME, revisión 0, usando su propia
  tabla de overlays y sus propios IDs FAT. Los 103 resultados coinciden byte a
  byte con el decodificador independiente ndspy 4.2.0.
- Descubrimiento del catálogo de handles mediante referencias a rutas reales
  NitroFS. Se exige una única tabla contigua, sin archivos repetidos. En ASME0
  contiene 1.824 entradas y empieza en `0x020b371c`.
- Descriptor original del exterior del castillo: overlay 9, dirección DS
  `0x021081fc`, 15 grupos de objetos y 14 entradas. Los IDs de sus cuatro
  recursos se resuelven por el catálogo, sin aplicar el desplazamiento EU.
- Dos globals que utiliza el código recompilado: la tabla trigonométrica
  `data_02082214` y el escalar del menú `data_ov007_0210325c`. La publicación es
  posterior a comprobar el perfil de ROM y el CRC de ambos fragmentos.

Los métodos `OverlayImage::range` y `load_level` entregan offsets y valores
enteros; los registros siguen perteneciendo a la imagen que los contiene. Las
coordenadas de `Entrance` conservan las unidades originales `s16`; la lógica
original las convierte a `Fix12` multiplicándolas por 4096. La selección de la
entrada, personaje, progreso y escena sigue siendo responsabilidad del juego.

## Uso

Agregar `game_data.cpp` y `native_constants.cpp` al target de datos. Antes de
llamar al código original, ejecutar `initialize_native_constants(pack,error)` y
detener el arranque si devuelve `false`. Actualmente la inicialización acepta
únicamente el ASME0 investigado, CRC32 de la ROM `e6321562`. La tabla de senos y
cosenos está en `0x0207fee0` (16.384 bytes, CRC32 `ee067d57`); el escalar del menú
está en overlay 7, `0x020f99bc` (4 bytes, CRC32 `3d627c6c`).

El resultado de la extracción anterior no equivale a tener un juego jugable.
Todavía hacen falta la inicialización del resto de globals, el enlace de las
rutinas originales, los servicios nativos y la adaptación de sus vtables al ABI
de GCC. Los punteros de métodos y factories de DS nunca pueden ejecutarse desde
las imágenes que carga este módulo.

## Evidencia y correspondencia regional

`tools/game_data_region_probe.py` compara funciones completas del corpus
`upstream/nearmiss/db.jsonl` con la ROM local. Sólo enmascara los campos que
`config/**/relocs.txt` identifica como relocalizaciones; exige una única
coincidencia para cada función. Produce un informe en `build/game-data/` sin
incluir bytes de la ROM. El resultado actual es 19 coincidencias completas de
26 funciones examinadas, que permiten inferir 129 direcciones sin conflictos.
Siete funciones independientes coinciden en la dirección US de la tabla
trigonométrica; sus 8.192 valores también coinciden con la cuantización entera
de seno/coseno de 4.096 pasos.

El informe es una frontera de investigación, no un mapa completo aceptado por
el runtime. No prueba tamaños, vtables ni equivalencia de todos los datos. Hay
una diferencia demostrada que impide usar un delta global: las tablas EU
`0x021000c0` y `0x021000d0` aparecen en US en `0x020f5ec4` y `0x020f5eb4`, en
orden inverso.

La ampliación `--source-rom build/roms/europe.nds` usa funciones completas de la
ROM ASMP0 verificada en vez del corpus reducido de near-misses. El informe
`build/game-data/full-region-probe.json` encuentra 3.623 coincidencias únicas
entre 4.450 candidatos y evidencia para 6.048 direcciones. Nueve direcciones
tienen destinos contradictorios y quedan señaladas como conflictos. Este
resultado tampoco autoriza cargar globals sin comprobar sus tamaños, tipos y
referencias; no se ha aplicado un delta global al juego.

SM64DSe Ultimate, commit `069442d74ad9004224ed8e0b9a976a41a469539d`, confirma
independientemente el catálogo US y ofrece las direcciones de las tablas de
niveles, pero su plantilla ASM sigue siendo EU. Referencias primarias:

- [NitroROM.cs: perfiles regionales y catálogo](https://github.com/Gota7/SM64DSe-Ultimate/blob/069442d74ad9004224ed8e0b9a976a41a469539d/src/core/NitroROM.cs)
- [NitroROM_patch.cs: tablas de niveles por región](https://github.com/Gota7/SM64DSe-Ultimate/blob/069442d74ad9004224ed8e0b9a976a41a469539d/src/core/NitroROM_patch.cs)
- [SM64DS ASM Reference: alcance EU declarado](https://github.com/SplattyDS/SM64DS-ASM-Reference)

## Comprobaciones reproducibles

`tools/game_data_test.cpp` comprueba secuencias BLZ válidas y corruptas, la
preservación de la salida al rechazar entradas y las comprobaciones de rangos.
`tools/game_data_probe.cpp assets.pak directorio` carga las constantes, el
catálogo y el castillo y exporta los overlays al directorio privado indicado.

Los artefactos locales de la prueba actual son `build/game-data/probe.txt`,
`verification.json`, `region-probe.json` y `native/overlay_*.bin`. El probe y las
pruebas se ejecutaron con AddressSanitizer y UndefinedBehaviorSanitizer. Ambos
módulos de datos también se compilaron para ARM11. Estas comprobaciones no
demuestran ejecución de menús, movimiento, audio o rendimiento en una 3DS.

## Relocalización nativa del registro de callbacks DSMT

`relocation.cpp` incorpora un relocalizador transaccional. Cada entrada declara
offset, palabra DS esperada, módulo, símbolo, tipo y desplazamiento interior.
El resolvedor devuelve una dirección nativa y, para datos, el tamaño real de
su almacenamiento. Se rechazan símbolos desconocidos, módulos equivocados,
punteros de funciones con desplazamientos interiores, datos fuera de su
almacenamiento, solapamientos, truncamiento y overflow de 32 bits. Los bits de
modo Thumb de las funciones se conservan. No se acepta devolver la misma
dirección original DS como si fuese una resolución nativa.

El llamante debe aportar un esquema completo de las palabras que son punteros:
el relocalizador no adivina que cualquier número situado en el rango RAM DS sea
un puntero. Todos los campos del registro DSMT de esta sección son conocidos.

`menu_callbacks.cpp` carga el registro `data_ov007_02103290` y el estado
`data_ov007_02103260`. La estructura `Arg` de
`upstream/src/func_ov007_020b7138.c` consta de seis palabras, y consume exactamente
24 bytes del registro. Los ocho bytes siguientes pertenecen al prefijo RTTI de
otro objeto; no se incluyen por usar ciegamente el delta al símbolo siguiente.

El registro US empieza en `0x020f99f0`, CRC32 `a0784624`. Se comprueba el perfil
completo ASME0, el contenido entero del registro, el nombre RTTI único
`9dScDSMT_c` en `0x020f99cc` y su referencia en `0x020f99dc`. El escalar de menú
ya identificado por una función completa está en `0x020f99bc`. El campo de
estado en `0x020f99c0` contiene el valor inicial -1; se lee de la ROM.

| Offset | Función EU identificada | Dirección US en el registro |
| --- | --- | --- |
| 0 | `func_ov007_020ccab4` | `0x020c3418` |
| 4 | `func_ov007_020cca98` | `0x020c33fc` |
| 8 | `func_ov007_020cca80` | `0x020c33e4` |
| 12 | `func_ov007_020cca74` | `0x020c33d8` |
| 16 | `func_ov007_020cca68` | `0x020c33cc` |
| 20 | `func_ov007_020cc600` | `0x020c2f64` |

La identidad de los slots procede de los seis `kind:load` del registro EU en
`config/arm9/overlays/ov007/relocs.txt`, contrastados con el registro DSMT US.
Los cinco wrappers pequeños US conservan las formas observadas en las fuentes:
asignación/liberación por heap, consulta de bloque máximo y dos saltos largos.
El sexto handler conserva al inicio los casos de selección de sonido 1/2/3
que presenta su fuente. No se aplica un delta regional a símbolos vecinos.

Para activar este grupo, enlazar `relocation.cpp` y `menu_callbacks.cpp` y llamar
a `initialize_menu_callbacks(pack,resolver,error)` antes del código original.
El resolvedor debe conocer las seis funciones compiladas y su módulo `ov007`.
Estos seis campos son callbacks directos, no una vtable C++: no llevan el ajuste
de ocho bytes del address point de GCC.

Las dos fuentes originales de saltos largos no declaraban sus argumentos:
`func_ov007_020cca74` debe conservar el handle al llamar a `LoadFile(int)`, y
`func_ov007_020cca68` debe conservar el puntero al llamar a `Deallocate(void*)`.
Además, un handle lógico EU no equivale a un FAT ID US; sigue siendo necesaria
su correspondencia verificada antes de ejecutar la ruta de carga completa.

`tools/game_data_relocation_test.cpp` prueba el relocalizador con direcciones
nativas sintéticas de 32 bits y, con `assets.pak` como argumento, carga el
registro real de la ROM. Confirma sus seis slots, el estado inicial y que un
resolvedor incompleto conserva el registro anterior. No ejecuta los callbacks.
Pasó con ASan/UBSan y los dos nuevos módulos compilan para ARM11.

El grupo también incluye los punteros iniciales `data_ov007_02103254` y
`data_ov007_02103258`: US `0x020f99b4..0x020f99bc`, CRC32 `2229e9ba`, con destinos
`0x020ba7ac` y `0x020ba7c4`. Corresponden a `func_ov007_020c3e4c` y
`func_ov007_020c3e64`. Sus cuerpos US se comprobaron: ambos preservan el argumento
entrante en r2, fijan r0=0/r1=-1 y saltan al servicio de liberación/asignación
respectivo. El wrapper de asignación debe devolver el resultado del servicio;
la declaración original `void` perdía ese contrato al recompilar con GCC.

`source/ctr/game_symbols.cpp` proporciona el resolvedor explícito
`resolve_native_game_symbol`, con referencias directas a las ocho funciones
compiladas. No enlaza funciones por sus direcciones DS. La firma del callback
de tamaño máximo también conserva su retorno: el consumidor
`func_ov007_020c3ce4` lo lee como un entero. La prueba con el paquete ASME0 valida
las ocho resoluciones y la publicación de los dos punteros adicionales.
