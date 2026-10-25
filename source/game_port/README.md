# Compilación nativa del código original

Esta capa prepara el código de juego para ARM11. Aún no forma parte del
ejecutable de presentación de 3DS: contiene accesos al hardware de DS que
requieren una implementación nativa antes de ejecutar el menú o la aventura.

El origen fijado es `tangosdev/sm64ds-decomp`, rama `port/release-0.4.0`, commit
`6230b2996636e3fd0e03cb178161c94cb4b73cc9`. Los scripts no modifican `upstream/`.

## Reproducir

Desde la raíz del proyecto, con devkitARM instalado:

```sh
python3 tools/native_core.py --jobs 4
python3 tools/native_core_inventory.py --link-frontier
```

El primer comando devuelve un código distinto de cero mientras haya fuentes
que no compilen; los diagnósticos completos quedan en
`build/native-core/report.json`. Esto es deliberado. El segundo realiza un
enlace **relocatable**, conserva las dependencias sin resolver y escribe
`build/native-core/gameplay-frontier.o`; ese archivo no es un ejecutable.

Para incorporar objetos nativos de plataforma/datos y sustituir explícitamente
un proveedor original:

```sh
python3 tools/native_core_inventory.py --link-frontier \
  --extra-object build/ruta/al/adaptador.o \
  --replace-source src/nombre_del_proveedor_original.cpp
```

Cada objeto adicional debe ser un ELF; se pueden repetir ambas opciones. No se
acepta un archivo de biblioteca como si fuera un objeto. Las bibliotecas de
plataforma se pueden aportar mediante sus objetos individuales. Un conflicto
entre definiciones fuertes impide el enlace; no se permite el modo de enlazado
que elige arbitrariamente entre varias definiciones.

## Adaptaciones comprobables

- `size_t` usa el tipo de la herramienta ARM11. Se mantienen las aserciones de
  tamaño y se activan las aserciones de desplazamiento de miembros también en
  `__3DS__`.
- Los constructores, destructores y ajustes de `this` que GCC emite juntos
  tienen un único propietario únicamente cuando sus unidades, después del
  preprocesador, contienen exactamente los mismos tokens. El registro guarda
  todas las unidades redundantes y el propietario elegido.
- Los 301 bloques de funciones de `actors/Player.cpp` se separan conservando
  sus cuerpos y el prefijo compartido. Se comprueba el símbolo, ordinal y
  dirección contra `config/tu_manifest.d/ov002/Player.json`. Así no se mezclan
  declaraciones locales incompatibles que admitía MWCC. Se quitan prototipos
  locales redundantes de la misma función; no se inventa su implementación.
- El borrado de los veinte bytes de la cola de objetos sostenidos del jugador
  se expresa en C. Las primitivas ARM de copia y matrices conservan sus
  instrucciones en sintaxis GAS, sin operaciones de sistema privilegiadas.
- `Stage::UpdateMessage` enlaza las implementaciones existentes de mensajes y
  guardado. Sus cuatro definiciones vacías locales originales se retiraron de
  la copia generada; de otro modo GCC eliminaba esas operaciones al optimizar.
- Los símbolos `data_020a4b6c` y `data_020a4ba8` son referencias externas. No se
  conservan sus definiciones accidentales de un byte: el inicializador
  `__sinit_02075154` escribe estructuras de doce y dieciséis bytes en ellas.
- `ProcessKuppaScript` conserva sus llamadas virtuales originales. Su clase
  local es una vista de una tabla de funciones, y la desvirtualización de GCC
  producía llamadas directas a sobrecargas que no existen.
- El destructor vacío de `Vector3` se exporta expresamente con el nombre ABI
  que necesitan las llamadas originales de limpieza de arrays. Su cuerpo vacío
  está documentado por el código original; no sustituye una operación pendiente.
- Tres cuerpos recuperados necesarios para el menú y los modelos se incluyen
  explícitamente aunque no figuran en `delinks.txt`: `ShadowModel::InitCylinder`,
  `Model::LoadCompressedTextureToVram` y `func_ov007_020bfd70`. El informe los
  distingue de las fuentes inscritas en el proceso de reproducción binaria.

## Alcance de la evidencia

La colección general todavía conserva los operadores globales del juego. No
debe enlazarse directamente con los contenedores C++ de la plataforma: hay que
aislar de forma coherente sus operadores de asignación/liberación. El subconjunto
del heap usado por el CIA ya tiene ese aislamiento en `build/native-heap/`;
`tools/test_game_heap_arm.py` audita definiciones y referencias y comprueba
liberación con y sin tamaño. Esto no significa que el resto del núcleo esté
aislado ni listo para ejecutarse.

`symbols.json` detalla los proveedores fuertes, referencias sin resolver,
dependencias del menú/jugador y accesos literales a registros de DS.
`counts.json` resume el resultado. La colección de dependencias es conservadora
entre objetos; las llamadas indirectas de tablas extraídas de la ROM todavía
pueden requerir más proveedores.

El archivo relocatable verifica compatibilidad de objetos y ausencia de
definiciones fuertes duplicadas en esa colección. No prueba arranque, interfaz,
movimiento, rendimiento ni compatibilidad con hardware físico.

Las tablas C++ generadas por GCC incluyen una cabecera antes del primer método.
Los alias de tablas originales y sus asignaciones a objetos deben respetar la
dirección del primer método, así como las tablas secundarias y los ajustes de
`this`. No basta con sustituir una dirección de ROM por el símbolo `_ZTV`.

Todavía requieren adaptación los accesos a memoria de DS, interrupciones,
contextos del sistema, carga de overlays, tablas con punteros, sonido y envío de
gráficos. El archivo original de datos corresponde a la versión europea;
resolver los datos de la ROM estadounidense requiere pruebas de correspondencia
específicas y no un desplazamiento global supuesto.
