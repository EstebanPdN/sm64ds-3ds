# Historical project entry point

Archived before the first source publication. These dated descriptions are
not the current installation or compatibility guide. Start at the
[current README](../../README.md).

# SM64DS para Nintendo 3DS — desarrollo

Port nativo en construcción a partir de
[tangosdev/sm64ds-decomp](https://github.com/tangosdev/sm64ds-decomp).
El objetivo es conservar el juego de DS, con Circle Pad, cámara con C-stick en
New 3DS, vista superior de 400×240 y estéreo, tomando Old 3DS como base.

This project was developed with the help of [OpenAI Codex](https://openai.com/codex/). Learn more about the development process [here](../../AI_DEVELOPMENT.md).

Estado consolidado: [inventario y tareas](../../docs/INVENTARIO_PORT_2026-10-21.md).
La estructura, los contratos y la preparación de candidatos sin emulador están
en [desarrollo y compilación](../../docs/DEVELOPMENT.md). Los apartados con fechas
anteriores conservan la evidencia de sus respectivos binarios.

## Instalación integrada desde la ROM

La nueva entrega **SM64DS Native — ROM Only** prepara automáticamente sus
datos desde cualquier archivo `.nds` dentro de `SD:/3ds/sm64ds/`
(Europe ASMP, revisión 0). El nombre es libre y también se admite `.NDS`.
La preparación escribe un solo `prepared.sm64ds`, igual al archivo generado
por SM64DS Packer, y el juego lo lee directamente para evitar miles de
creaciones de archivos y sincronizaciones en la SD.
Incluye una pantalla
de carga con el arte y las fuentes originales, progreso real y selección del
idioma de la consola: inglés, español, portugués, francés, italiano y alemán,
con inglés para los demás idiomas. No necesita copiar recursos
extraídos ni preparar un archivo DSP manualmente. Las instrucciones, memoria,
recuperación y límites de validación están en
[ROM_ONLY_INSTALL.md](../../docs/ROM_ONLY_INSTALL.md).

Se compila con `tools/build_integrated_native.py --render60 --rom-only` o el
target `sm64ds-integrated-rom-only_3dsx`. El CIA integrado usa el Title ID
experimental `000400000F536400`; el visor antiguo se conserva por separado.

## Candidato integrado — 15 de octubre de 2026

El target `sm64ds-integrated_3dsx` incorpora como conjunto coherente la
plataforma nativa de [Sébastien Breton](https://github.com/SebastienBreton/sm64ds-3ds),
fijada en `88725627c529e5d9db57abc5544513d8741fa07d`, con licencia MIT y sus
créditos conservados en `third_party/sm64ds-3ds/LICENSE`. Su dependencia de
decompilación se fija por separado en `reference-native.lock.json`; el
`upstream.lock` original y el runtime experimental se conservan.

Incluye el renderizado GPU y composición 2D, sombras, audio SDAT/DSP,
actores/overlays, guardado, Circle Pad analógico, cámara C-stick y opciones
de presentación del proyecto de referencia. El paquete nuevo selecciona
`camera_cstick = free`. La comprobación previa a emulación compara los 11.204
bytes de guiones empaquetados y sus 61 punteros reales, los 64 comandos de la
secuencia que sufrió reubicaciones incorrectas y el par ELF/3DSX. Los controles
pasan un gate host con ASan/UBSan. La adquisición Old de referencia ya mostró
la introducción, Yoshi controlable, desplazamiento y salto. Esto todavía no
demuestra gameplay completo, audio audible, cámara física ni 60 FPS en Old 3DS.

El target experimental `sm64ds-integrated-60_3dsx` agrega fotogramas visuales
intermedios sin duplicar la simulación. Su implementación, gates y evidencia
por escenario están en [INTEGRATED_NATIVE.md](../../docs/INTEGRATED_NATIVE.md).

Compilación local reproducible, con la ROM EU privada ya verificada:

```sh
cmake -S . -B build/integrated-native-driver
cmake --build build/integrated-native-driver --target sm64ds-integrated_3dsx
```

El resultado y su árbol SD privado quedan en `build/integrated-native/candidate-*`;
`latest.json` identifica el candidato y sus hashes. El controlador prepara
dependencias Python aisladas y el checkout público fijado, extrae localmente
la ROM y no abre Azahar. `tools/run_integrated_native.py` exige los gates
antes de una adquisición en un perfil independiente. El CIA histórico de
`dist/` sigue siendo el visor descrito abajo.

## Runtime y visor anteriores

**El CIA histórico del visor es una prueba de importación y dibujo del castillo. Aún no
ejecuta el menú original, una partida ni el personaje. No es el juego terminado.**
Compilar el código original no elimina sus accesos a hardware DS ni convierte
los punteros de una ROM en direcciones nativas válidas.

El runtime experimental anterior es un target separado, `sm64ds-native.3dsx`.
Ya restaura los datos generales de la ROM EU aportada y llega al despacho de
la primera escena original en Azahar Old. **Todavía no tiene menú interactivo
ni partida verificados.** Sus instrucciones, servicios integrados y evidencia
están en [NATIVE_RUNTIME.md](../../docs/NATIVE_RUNTIME.md); no reemplaza el CIA del
visor descrito a continuación.

## Probar el visor histórico

- Binario: `dist/sm64ds-native-preview/sm64ds-preview.cia`.
- ROM del usuario: **`/3ds/sm64ds/rom.nds`** en la tarjeta SD.
- Recursos extraídos: `/3ds/sm64ds/assets.pak`.
- Registro del arranque: `/3ds/sm64ds/platform.log`.

Instalar el CIA con el instalador homebrew habitual y abrirlo. La primera vez
extrae y verifica los recursos; B cancela esa extracción. La ROM se conserva.
El perfil de datos investigado y admitido por el arranque actual es USA ASME,
revisión 0, CRC32 `e6321562`. No se incluyen ROM ni recursos extraídos.

En el visor, Circle Pad desplaza la vista; C-stick o cruceta gira la cámara;
L/R acerca o aleja; A centra y START sale. El slider 3D activa dos vistas
distintas. Estos controles todavía no manejan a un personaje.

## Estado técnico del visor disponible

- Importación NitroFS y segmentos necesarios del cartucho, con validación de
  rutas, tamaños y CRC, publicación mediante archivo temporal y cancelación.
- Dibujo nativo con Citro3D/PICA del BMD original del castillo, materiales y
  texturas. La proyección usa 400/240; no estira un framebuffer de DS.
- Lectura verificada de los 103 overlays y del catálogo de 1.824 handles USA.
  El CIA también inicializa las constantes originales y comprueba el descriptor
  del castillo (15 grupos y 14 entradas).
- Código del menú, Stage y Player compilado para ARM11 con comprobaciones de
  layouts. Selección de destructores equivalentes, correcciones explícitas de
  ABI y arenas BSS con offsets originales. Los fallos de compilación restantes
  aparecen en el informe, sin convertirse en implementaciones vacías.
- Adaptadores nativos de matemáticas, primitivas de memoria, carga de archivos,
  entrada analógica y reubicación de ocho callbacks del menú. Esta parte se
  verifica como objeto relocatable separado; todavía no se ejecuta como juego.
- Gestor de memoria original aislado de las asignaciones de la interfaz nativa.
  El CIA preparado inicia una arena de 4 MiB y verifica asignación, escritura,
  liberación y recuperación del espacio antes de cargar los datos del juego.

Faltan servicios gráficos/2D, sincronización y otros accesos de hardware DS;
tablas inicializadas y vtables; catálogo EU→USA completo; arranque del menú y la
aventura; guardado/audio y validación de gameplay. El cuerpo de simulación
original debe conservar su ritmo: dibujar a 60 Hz no autoriza duplicar la
velocidad de la lógica. **No hay medición física de 60 FPS en Old o New 3DS.**

## Compilar y comprobar

Se requieren CMake, Python 3 y devkitPro/devkitARM con libctru y citro3d para
3DS. `upstream.lock` fija las revisiones usadas. `upstream/` contiene el checkout
local de la decompilación y está excluido del repositorio de este port.

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure

cmake -S . -B build/3ds \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/3DS.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/3ds --parallel 4
```

Para el código original, después de preparar el checkout fijado:

```sh
python3 tools/native_core.py --jobs 4
python3 tools/link_native.py
```

El primer comando termina con código 1 mientras existan fuentes no compiladas,
y escribe sus diagnósticos en `build/native-core/report.json`. El segundo
reconstruye la selección inicial, genera BSS, aplica las sustituciones explícitas
y enlaza `gameplay-frontier.o` con advertencias fatales. Ese `.o` admite símbolos
pendientes y **no es un ejecutable ni el CIA**. Los números y dependencias
vigentes están en `build/native-core/counts.json` y `symbols.json`.

El CIA de esta sesión se compila además con el heap original. Para reproducirlo,
instalar `unicorn==2.1.4` en el entorno Python de pruebas y ejecutar, después de
los pasos anteriores:

```sh
python3 tools/test_game_heap_arm.py
cmake -S . -B build/3ds -DSM64DS_ORIGINAL_HEAP=ON
cmake --build build/3ds --parallel 4
```

El manifest generado incluye únicamente los objetos necesarios y aísla sus
operadores `delete`. CMake rechaza un manifest sin ese aislamiento. Para rehacer
desde cero los informes, partir primero de `SM64DS_ORIGINAL_HEAP=OFF` y activar
la opción cuando la prueba haya generado el manifest.

Detalles: [empaquetado y pruebas CIA](../../docs/PACKAGING.md),
[carga nativa](../../docs/NATIVE_FILES.md), [primitivas](../../docs/NATIVE_PRIMITIVES.md),
[datos regionales](../../source/game_data/README.md) y
[adaptación del código original](../../source/game_port/README.md).

La continuación incorpora un conjunto nativo de colisiones que ya ejecuta las
consultas originales de suelo/esfera sobre el castillo real en pruebas ARM11.
El ELF/3DSX puede incluirlo; el CIA de distribución sigue siendo el paquete
anterior. No añade todavía Player ni una partida. Reproducción, una vez
preparados `native-core`, `native-globals` y el heap como se indica arriba:

```sh
cmake --build build/sanitize --parallel 4
python3 tools/build_game_collision.py
build/sanitize/test_collision_data build/assets.pak build/native-collision
/tmp/sm64ds-primitives-tests/bin/python tools/test_game_collision_arm.py
cmake -S . -B build/3ds -DSM64DS_ORIGINAL_HEAP=ON -DSM64DS_ORIGINAL_COLLISION=ON
cmake --build build/3ds --parallel 4
```

El entorno Python de ese ejemplo es el de Unicorn descrito en
`docs/NATIVE_PRIMITIVES.md`. Los informes y las limitaciones de esta continuación
están en [estado comprobado](../../docs/STATUS.md).

## Evidencia y créditos

La continuación del jugador y sus límites están documentados en
[NATIVE_PLAYER.md](../../docs/NATIVE_PLAYER.md): constructor original ejecutado como
ARM11 en Unicorn, cargador de 198 recursos NARC verificado contra ambas ROM,
recursos del jugador inicializados y 180 ciclos aislados de reposo, movimiento
y frenado sobre el castillo. El arranque normal se detiene en la preparación
del audio. El nuevo ELF/3DSX
incorpora el servicio de archivos; todavía no ejecuta una partida y conserva
pendientes las pruebas de emulador y consola física de ese binario.

Las pruebas host incluyen AddressSanitizer/UndefinedBehaviorSanitizer. Las
primitivas se prueban también como instrucciones ARM. Los informes de Azahar
separan la configuración emulada, el hash del binario, la importación, el dibujo
y la salida. Ninguna de esas pruebas representa una consola física.

Código base y decodificadores: Tango y colaboradores de `sm64ds-decomp`, con
la licencia MIT conservada en `source/vendor/ntr/LICENSE`. Bibliotecas de la
plataforma: devkitPro, libctru y citro3d. Los assets originales se obtienen
únicamente de la ROM que aporta el usuario.
