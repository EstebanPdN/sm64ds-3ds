# Integración del jugador: evidencia del 9 de octubre de 2026

El constructor original de `Player` se ejecuta como instrucciones ARM11 en
Unicorn. **El ejecutable de 3DS todavía no ejecuta al jugador ni permite empezar
partida.** La inicialización de recursos y 180 ciclos aislados de reposo,
movimiento y frenado ya terminan en el diagnóstico ARM11, con archivos reales y
colisión contra el castillo. El arranque normal de partida todavía encuentra un
acceso a servicios DS durante la carga de sonido.

## Cambios en la recompilación

`tools/native_core.py` conserva únicamente el símbolo C1 o C2 que corresponde a
cada unidad original del constructor `dActor_c`. GCC emitía ambos símbolos en
ambas unidades y provocaba un conflicto de definiciones fuertes. No se descarta
el cuerpo de ninguna variante. Se añade también la declaración que faltaba del
inicializador de paleta en `__sinit_ov002_021019d0.c`; esta omisión impedía
compilar la rutina que construye las referencias a archivos y estados del jugador.

La opción `--only-source` permite recompilar fuentes concretas conservando el
informe completo. Antes de reutilizar objetos exige el mismo commit, opciones,
cabeceras y texto adaptado de las fuentes no seleccionadas. El corpus actual
compila 8.784 de 8.872 unidades seleccionadas; las 88 restantes siguen registradas
como errores. Un resultado no cero del corpus completo no convierte esas
fuentes pendientes en implementaciones disponibles.

## Ejecución limitada del constructor

`tools/diagnose_player_arm.py` enlaza un ejecutable de diagnóstico separado de
la aplicación 3DS. Lee la ROM europea ASMP0 aportada, comprueba su CRC y carga
las secciones de datos declaradas de ARM9 y overlays 0, 1, 2 y 9 en almacenamiento
reservado por el enlazador. Reubica 7.837 referencias declaradas por los metadatos fijados. No
mapea código DS como ejecutable; las secciones importadas son datos sin permiso
de ejecución. Las regiones de registros DS permanecen sin mapear.

Los métodos indirectos no disponibles apuntan a trampas que detienen la prueba
con su nombre. Los destinos de datos todavía no admitidos quedan sin mapear.
Estas trampas **son exclusivas del diagnóstico**: no devuelven valores falsos,
no implementan servicios y no se incluyen en el ELF/3DSX de la aplicación.

`native_data_layout.py` genera 13 secciones nativas vacías y conserva los offsets
de sus aliases. `sm64ds_restore_native_data`, compilado para ARM11 desde
`source/core/native_data.cpp`, copia e inicializa 359.736 bytes y aplica las
reubicaciones. El host proporciona las fuentes y resuelve símbolos del ELF;
ya no escribe directamente los datos finales del juego. Se compara cada byte
del resultado con una reconstrucción independiente. Alterar la última referencia
provoca un rechazo que conserva intactas las 13 secciones. El cargador también
rechaza destinos solapados, fuentes que se sobrescribirían, tablas que serían
alteradas por la copia y offsets inválidos. Está compilado como biblioteca de
3DS; su enlace al arranque completo de Player sigue pendiente.

Los objetos originales pasan por `native_operators.py`: sus operadores escalares
`new`, `delete` y `delete` con tamaño reciben nombres privados antes de enlazar
el runtime C++. Un operador no aislado detiene la construcción. Así se evita que
el asignador original capture las asignaciones de la plataforma. Esta regla se
aplica también a los constructores y destructores generados por GCC.

El informe `build/player-diagnostic/execution.json` comprueba:

- Ejecución de los inicializadores originales de listas de actores y del
  constructor `Player`, con 29 funciones distintas visitadas.
- Posición de aparición `(100, 254, -100)` en unidades originales, conservada
  como valores Fix12; rotación actual y anterior conservadas.
- Perfil original PLAYER `0xbf`, ID único inicial leído de los datos de ROM y
  parámetro del actor conservados. Se comprueba que la tabla de aparición apunta
  al perfil real del overlay 2; usar el perfil 0 cambia los límites de visibilidad.
- Matrices de ambos modelos copiadas de la identidad original.
- Objeto rellenado previamente con `0xa5`, canario posterior intacto, pila y
  registros de preservación obligatoria intactos; ninguna trampa alcanzada.

## Inicialización de recursos y transferencias gráficas

Con `--resources`, el diagnóstico ejecuta los 26 inicializadores estáticos del
overlay 2, la factoría original `daPly_c_classInit` y `Player::InitResources`.
El catálogo EU de 2.058 handles se construye desde rutas verificadas de la ROM.
La factoría usa el asignador original de `fBase_c`, incluido su borrado inicial.
Se registra el KCL/CLPS real en la lista original de colisiones y se selecciona
el descriptor original del castillo. La fase termina con resultado 1 y posición
`(-1200, 254, 6800)`, sobre el suelo de la primera entrada original.

`build/player-resources-diagnostic/execution.json` registra 98 solicitudes de
archivos y 257 funciones distintas visitadas, sin trampas ni accesos inválidos.
El anfitrión proporciona únicamente los bytes de la ROM en el punto de E/S;
el puente ARM11 realiza la asignación y copia, y los cargadores originales
interpretan los modelos, animaciones y secuencias.

`GameGeometry` sustituye la selección de bancos y las transferencias GX de
texturas/paletas por buffers propios, con límites y fases de transferencia
comprobados. El asignador original del modelo sigue eligiendo sus offsets.
Los bancos de Stage habilitan 262.144 bytes de texturas y 81.920 de paletas.
La ejecución realiza 120 transferencias de texturas (39.392 bytes) y 62 de
paletas (12.688 bytes); ambos buffers completos coinciden byte por byte con
las transferencias solicitadas por los cargadores originales. El informe
conserva los hashes de cada transferencia y del contenido final.

Se conserva la creación original de la paleta gris. La selección de sombreado
que leía `0x04000060` se almacena ahora como estado nativo. **Esto verifica carga
y almacenamiento de recursos; no demuestra su dibujo ni sombreado en PICA.**
El puente está compilado para 3DS, pero aún falta conectarlo a un frame del juego.

## Prueba de lógica aislada y arranque normal

`--ticks 120` activa el contexto normal de sesión antes de `InitResources`.
Esta ruta todavía se detiene al leer `0x027ffc40` en `func_0203d974`, dentro de la
preparación de bancos de sonido. El informe separado
`build/player-motion-diagnostic/execution.json` conserva ese bloqueo.

`--ticks 180 --isolated-input` es una prueba independiente: activa la entrada
DESPUÉS de cargar recursos, sin inicializar el audio de personaje ni arrancar
Stage desde el menú. Prepara un área visible, una vista fija, el Clipper original
y un objeto Camera construido por su factoría. No ejecuta los recursos ni la
lógica de Camera. Ejecuta `Stage::CheckCameraInput`, `Stage::CheckInput`, `dActor_c::BeforeBehavior`,
`Player::Behavior` y `dActor_c::AfterBehavior` en su orden original.

La fase anterior al comportamiento es necesaria: guarda la posición previa
que utiliza la colisión continua. La primera prueba que la omitía desplazaba
al jugador desde un origen incorrecto; ese resultado se rechazó. La versión
corregida completa las tres fases, registradas en
`build/player-motion-isolated-diagnostic/execution.json`:

- 60 ciclos sin entrada: conserva `(-1200, 254, 6800)`.
- 60 ciclos con derecha: avanza 443,8984375 unidades del juego en X, manteniendo
  la altura del suelo y Z. Hay un único flanco de pulsación al comenzar.
- 60 ciclos tras soltar: frena y conserva la posición final
  `(-704,12109375; 254; 6800)` en los últimos diez ciclos, con velocidad cero.

La ejecución visita 496 funciones distintas y solicita 101 archivos, sin
trampas ni accesos inválidos. No equivale a iniciar una partida, recorrer el
mapa en 3DS, ejecutar audio ni verificar el control analógico.

La rutina central de caminar procede de
`upstream/port/unmatched/func_ov002_020d3b9c.c`, la adaptación de upstream que
**no es byte-matched**. Se mantiene su procedencia y hash en el manifest. Su
inclusión no prueba equivalencia de todas las físicas con el juego de DS.

Se añadieron tres constructores de matrices de rotación y la normalización de
vectores sin registros DS. Las rotaciones coinciden con las rutinas Thumb de la
ROM en 3.192 comparaciones. La normalización coincide en 4.686 casos, incluido
uso in-place, contra la rutina ARM de la ROM con sus registros matemáticos
modelados sólo para ese oráculo. Se conservan sus redondeos y desbordamientos
de registros. El diagnóstico de Player mantiene toda E/S DS sin mapear.
El cálculo de distancia usado para descartar sonidos también coincide en 2.343
casos con su rutina original. Esto sustituye una operación matemática; no
implementa la reproducción de sonido.

## Dibujo original y prueba separada de PICA

El diagnóstico `--ticks 180 --isolated-input --draw` añade `Player::Render`
después de la lógica original de cada ciclo. Termina los 180 ciclos, visita 521
funciones distintas y produce 4.965 palabras GX por ciclo, sin trampas ni accesos
inválidos. La adaptación conserva el cuerpo de renderizado del juego; sustituye
las escrituras a puertos y las transferencias de listas gráficas de DS. También
hace explícito el índice de material que el compilador original pasaba en r1.
No vuelve a calcular la pose al dibujar: usa las matrices que produjo Behavior.

Se decodificaron seis muestras con el lector GX y los buffers nativos de texturas
y paletas: cada una contiene 456 triángulos y tres texturas distintas. La imagen
`build/player-render-diagnostic/player-poses.png` es una inspección de geometría
en host; no es una captura de Azahar ni una partida. Los informes y entradas
están en `build/player-render-diagnostic/`.

`sm64ds-render-check.3dsx` es una aplicación separada que reproduce tres de esas
poses mediante Citro3D. No enlaza ni ejecuta la lógica de Player. La segunda
ejecución de Azahar, con perfil Old, registró ambas vistas, 456 triángulos por
muestra y `exit=clean rendered=3600 error=`. El proceso de Azahar permaneció
abierto y el supervisor lo terminó al alcanzar su límite; su código es -9,
distinto de la salida limpia registrada por la aplicación. La imagen congelada
ejecutada tiene SHA-256
`0a1dd90c05df2f7fe8c9210b059d4151da81a2fc05cb6d4913d6ba9edf579023`.

Evidencia: `build/player-render-azahar/result.json`. No se verificó visualmente
la ventana. Este resultado acredita ejecución y envío de geometría al PICA
emulado; no acredita una partida, corrección visual completa, FPS del juego ni
rendimiento físico. El primer intento sin ejecución confirmada queda conservado
en `attempt-locked/`. No hace falta repetir esta prueba para obtener la misma
evidencia.

La resolución de texturas admite los siete formatos DS y generación de UV de
modo 1. Los modos 2 y 3 se rechazan explícitamente; sombreado, sombras y todos
los materiales del juego aún no están validados. El CIA disponible sigue siendo
el visor anterior.

## Recursos del jugador disponibles en el cargador nativo

`GameFiles` admite ahora los IDs `0x8000..0x80c5` del archivo
`ARCHIVE/arc0.narc`: sus 321.224 bytes y CRC32 `e96c144b` son idénticos en las
ROM verificadas ASME0 y ASMP0. Sus 198 miembros contienen, entre otros, los
modelos de los cuatro personajes, animaciones y secuencias de texturas.

`parse_narc` comprueba cabecera, versión, tamaños, los tres chunks, cantidad de
miembros, rangos y solapamientos. No interpreta la tabla de nombres porque la
interfaz del juego accede por índice. El lector conserva la salida previa al
rechazar un archivo y aplica la descompresión `LZ77` que utiliza `SharedFilePtr`.

Los 198 resultados de cada ROM se compararon por longitud y CRC32 con ndspy,
incluida su descompresión independiente: 459.290 bytes decodificados por ROM,
todas las comparaciones coincidentes. El parser pasó además 21 casos inválidos
con ASan/UBSan. El caché conserva una copia comprimida del archivo; no mantiene
simultáneamente los 198 recursos decodificados.

El arranque del nuevo ELF/3DSX inicializa este servicio y lo enlaza al puente de
archivos original. El mensaje de log declara `gameplay_executed=0`. Se compiló
para ARM11, pero este nuevo binario todavía no se ha ejecutado en Azahar ni en
consola física. El CIA de `dist/` conserva la versión anterior.

Los otros archivos NARC requieren correspondencia regional propia: la tabla EU
tiene 13 entradas y la USA tiene 8, con bases de IDs y contenidos distintos.
El servicio rechaza los rangos no verificados; no aplica un desplazamiento
numérico común.

## Comprobaciones reproducibles

```sh
cmake --build build/sanitize --parallel 4
ctest --test-dir build/sanitize --output-on-failure
PYTHONPATH=build/game-data/python python3 tools/test_game_archives.py \
  --case 'Super Mario 64 DS.nds' build/assets.pak \
  --case build/roms/europe.nds build/europe.pak
/tmp/sm64ds-primitives-tests/bin/python tools/diagnose_player_arm.py
/tmp/sm64ds-primitives-tests/bin/python tools/diagnose_player_arm.py --resources
/tmp/sm64ds-primitives-tests/bin/python tools/test_rotation_primitives_arm.py
/tmp/sm64ds-primitives-tests/bin/python tools/test_normalize_arm.py
# El arranque normal conserva el fallo de sonido documentado:
/tmp/sm64ds-primitives-tests/bin/python tools/diagnose_player_arm.py --ticks 120
# Fixture de lógica independiente, con las limitaciones descritas arriba:
/tmp/sm64ds-primitives-tests/bin/python tools/diagnose_player_arm.py --ticks 180 --isolated-input
cmake --build build/3ds --parallel 4
```

La CPU anfitriona/Unicorn, Azahar y el hardware físico son tres niveles de
verificación distintos. Pasan las trece pruebas CTest con ASan/UBSan. No hay una
medición nueva de FPS ni velocidad de juego,
y los objetivos de 60 FPS estables y jugabilidad completa siguen pendientes.
