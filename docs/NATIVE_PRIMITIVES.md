# Primitivas CPU de la integración nativa

Estos tres símbolos cubren dependencias reales que quedaban sin definición en el análisis de enlace del juego. No añaden comportamiento ficticio ni activan por sí solos el menú o la aventura.

La referencia es `tangosdev/sm64ds-decomp` en el commit `6230b2996636e3fd0e03cb178161c94cb4b73cc9`, archivos `src/CpuCopy8.c`, `src/DecompressLZ16.c` y `src/Matrix3x3_SetRotationZ.c`. Las dos primeras funciones contienen instrucciones ARM originales; la tercera contiene instrucciones Thumb originales. El aviso MIT de upstream está conservado en `source/vendor/ntr/LICENSE`.

## Implementación

- `source/ctr/game_memory_primitives.S` convierte `CpuCopy8` y `DecompressLZ16` a sintaxis GNU ARM, con secciones independientes para el enlazador. Solo se adaptan directivas, etiquetas locales y el orden del sufijo condicional de las cargas/escrituras de halfword. Se conservan instrucciones, registros, flags, anchuras de acceso y orden de ejecución. No hay emulación de CPU dentro de la aplicación: estas instrucciones se ejecutan directamente en ARM11.
- `source/ctr/game_primitives.cpp` implementa `Matrix3x3_SetRotationZ` con nueve valores fx32: `{cos, sin, 0, -sin, cos, 0, 0, 0, 4096}`. La negación conserva el desbordamiento módulo 2^32 del `NEG` original, incluido `INT32_MIN`, sin introducir desbordamiento indefinido de C++.
- `include/sm64ds/game_primitives.hpp` declara los tres ABI y sus precondiciones. Todos los parámetros escalares son de 32 bits y los símbolos mantienen enlace C.

Las rutinas solo acceden a memoria suministrada por el llamante y a registros de CPU sin privilegios. **No convierten una dirección de VRAM o registro de DS en memoria válida de 3DS.** Los sitios originales que pasan direcciones como `0x06400000` requieren adaptación del renderer antes de ejecutarse. El análisis de enlace debe seguir señalando esos sitios.

## Contratos conservados

`CpuCopy8(source, destination, size)` copia exactamente el intervalo solicitado sin cambiar los bytes vecinos. La implementación original hace lecturas y escrituras de halfwords para los bordes no alineados, y de palabras cuando es posible. Los halfwords que contienen esos bordes deben ser accesibles. La interfaz no garantiza `memmove`: se usa con buffers separados o idénticos. El parámetro es número de bytes, independientemente de nombres incorrectos de parámetros encontrados en algunas declaraciones recuperadas.

`DecompressLZ16` recibe un stream LZ10 con cabecera de cuatro bytes y un destino alineado a dos bytes. Conserva el acumulador de un byte pendiente y las escrituras de dos bytes. El último byte de una longitud impar no se escribe. También conserva que un token final se procesa completo, aunque el stream defectuoso anuncie una longitud menor. Estos detalles difieren del reemplazo byte a byte del port de PC.

Esta firma histórica no contiene capacidades de buffers ni valor de error. **No es un parser acotado para entrada arbitraria.** El cargador que lo invoque deberá validar el stream y los buffers previamente; una futura interfaz nueva puede ofrecer ese control, pero no se inventa aquí un éxito silencioso ni se modifica la semántica de los llamantes existentes. Los tests incluyen la peculiaridad del token que excede la longitud declarada para detectar una modificación accidental, no para autorizar su uso con archivos malformados.

## Pruebas

Prueba host de la matriz con sanitizadores:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iinclude source/ctr/game_primitives.cpp tests/test_primitives.cpp \
  -o /tmp/sm64ds-test-primitives
/tmp/sm64ds-test-primitives
```

Prueba del código máquina ARM compilado para ARMv6K mediante devkitARM:

```sh
python3 -m venv /tmp/sm64ds-primitives-tests
/tmp/sm64ds-primitives-tests/bin/python -m pip install unicorn==2.1.4
/tmp/sm64ds-primitives-tests/bin/python tests/test_primitives_arm.py
```

Unicorn se utiliza únicamente en las pruebas del host; no se enlaza ni se distribuye en la aplicación. El test compila las fuentes reales, ejecuta sus instrucciones en modo usuario y comprueba el retorno, SP, los registros conservados r4–r11, el contenido de los buffers, los bytes de guarda y las anchuras de escritura.

Resultado observado: **753 casos de copia**, **8 casos de LZ16** y **49 casos de matriz** pasaron ejecutando el código ARM. Las retroreferencias LZ16 cubren distintas paridades, repetición solapada dentro de la salida y desplazamiento de 4.096 bytes. La matriz pasó además 49 casos host con AddressSanitizer y UndefinedBehaviorSanitizer. Hash del binario ARM aislado probado: `bb5ec70871fec634c7e86f6f9bc888de96c41ff64443ff6a7d77a6010eb7e97d`.

Esto verifica estas funciones y sus contratos locales. No prueba el enlace completo del juego, la validez de los destinos DS de todos sus llamantes, el arranque de la aventura ni rendimiento en hardware físico.

## Constructores de matrices alcanzados por el menú

`source/ctr/game_memory_primitives.S` incorpora cuatro hojas adicionales, usando directamente sus instrucciones ARM o Thumb originales del mismo commit y de la ROM aportada:

| Símbolo | Datos escritos | Contrato |
| --- | --- | --- |
| `func_02052ec8` | 16 palabras, 64 bytes | Identidad 4×4: índices 0/5/10/15 = 4096; demás = 0. |
| `func_02052550` | 9 palabras, 36 bytes | Rotación X de 3×3: `{4096,0,0,0,cos,sin,0,-sin,cos}`. |
| `func_0205256c` | 9 palabras, 36 bytes | Rotación Y de 3×3: `{cos,0,-sin,0,4096,0,sin,0,cos}`. |
| `func_020527e8` | 12 palabras, 48 bytes | Escala 4×3: `{x,0,0,0,y,0,0,0,z,0,0,0}`. |

Todos los destinos deben estar alineados a cuatro bytes, ser memoria real accesible y contener la capacidad indicada. Las negaciones de las rotaciones conservan el comportamiento módulo 2^32 de `NEG`, incluso con `INT32_MIN`. Se conserva el orden de escrituras, el avance original de `r0` y los efectos sobre registros de llamante y flags. No hay lecturas de datos, MMIO ni instrucciones privilegiadas en estas hojas.

La llamada `func_ov007_020c0128`, dentro de la preparación de la cámara de DSMT, requiere `Matrix4x3_LoadIdentity(c+0x44)` y `func_02052ec8(c+0x74)`. La primera ya está adaptada como ensamblador puro por `tools/native_core.py`; no se añade otra definición. La segunda había quedado sin definición real y fue el STOP nominal observado al avanzar desde Boot hacia `DSMT::InitResources`. `func_ov007_020c3abc` requiere las dos rotaciones 3×3. La hoja de escala está referenciada por `Matrix4x3_ApplyInPlaceToScale` y por el bloque de modelos. Las identidades 3×3/4×3 existentes permanecen en el corpus generado con sus propietarios actuales.

La prueba reproducible es:

```sh
/tmp/sm64ds-primitives-tests/bin/python tools/test_identity_primitives_arm.py
```

El arnés compila las fuentes actuales para ARMv6K y compara las seis hojas (las cuatro nuevas más las identidades 3×3/4×3 existentes) con los bytes de la ROM ASMP0 cuyo SHA-256 es `ce5829aaa79f06c67140ed1f45cbca738b0a9d0180538c8c1498472d859db032`. **Las instrucciones de las seis hojas son idénticas a las de la ROM.** Después ejecuta **7.024 comparaciones** de hojas CPU con Unicorn y una referencia Python independiente. Incluye extremos enteros, posiciones alineadas, destinos que cruzan páginas y datos iniciales variados. Comprueba todos los bytes vecinos, cada escritura de palabra, la ausencia de lecturas de datos, SP/r4–r11 y los valores exactos de r0–r3/ip/NZCV al retornar. El informe queda en `build/native-identities/report.json`.

La regresión de copia/LZ16/RotationZ conserva sus 753/8/49 casos y su hash original. Su enlace aislado usa ahora raíces explícitas y eliminación de secciones para probar únicamente esas tres hojas, sin arrastrar referencias libc de otros constructores que comparten archivo.

Estas comprobaciones pertenecen al host y al oráculo CPU de hojas. No ejecutan una escena completa ni prueban navegación, primera partida, Azahar, consola física o FPS.
