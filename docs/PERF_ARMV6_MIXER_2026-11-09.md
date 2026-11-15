# Rendimiento: mezclador ARMv6 y tiempo de audio en los informes (2026-11-09)

Candidato experimental **BUILD 130-armv6**, solo local: sin push, tag ni release.
La versión oficial sigue siendo `SM64DS_BUILD_NUMBER 130`
(`source/integrated_native/options/build_version.hpp`, sin tocar). Solo la copia
generada lleva la etiqueta. Commits de control en la rama privada
`perf/armv6-2026-11-09`; `main`, el índice y el árbol de trabajo no se tocaron.

**Las cifras de este documento son instrucciones ARM ejecutadas (Unicorn) o
volcados de consola ya existentes.** Azahar y el Mac no miden rendimiento de
consola. Las mejoras reales en Old y New 3DS están por medir con los volcados
de la sección «Qué medir en consola».

## ¿Corre en Old 3DS?

Sí. Las pruebas de consola de BUILD 123 y BUILD 128 recorren los 50 niveles en
Old 3DS a una media de 27,5 y 23,9 FPS. BUILD 130 vuelve a la distribución de
núcleos de BUILD 123. La columna Old cuenta.

## Diagnóstico

Ordenado por valor. Las rutas `G/` son del árbol generado
`build/native-renderer/game-runtime-20261026-r1/source/port/`, compilado tal
cual. `R/` es `source/integrated_native/renderer/`.

| Técnica | Estado | Evidencia | Valor Old / New | Riesgo |
|---|---|---|---|---|
| A. Mezclador ARMv6 (SMLAWB, SSAT, PKHBT, bucles especializados) | **Faltaba → aplicado** | El mezclador es una emulación del SPU del DS en enteros (int32/int64, 16 voces): `G/third_party/sseqplayer/Player.cpp:469-598` (`SoundPool::GenerateSamples`, ruta por segmentos activada por `G/platform/3ds/renderer_defaults.h:19`). Consola BUILD 128: `audioLoad=27.8%` de un núcleo (`dumps/native-test-anterior.log:843`). Unicorn: el 85 % de las instrucciones va en el bucle por voz `Player.cpp:533-547`, unas 45 instrucciones por voz y muestra con 8 volcados a la pila, dos `smull` de 64 bits y ramas invariantes. | **Alto Old** (núcleo 1 limitado al 30 %, o núcleo 0 con render en núcleo 1) / medio New | Bajo: idéntico bit a bit (Unicorn), interruptor en la SD |
| Telemetría del mezclador en los informes | **Faltaba → aplicada** | `resumen.txt` no tiene audio (`R/game_runtime.cpp:1706-1745`). `audioLoad` solo sale en el log al salir (`G/platform/3ds/player_present.cpp:690-692`). | Alto Old y New (sin ella no se puede medir A) | Nulo |
| Bloqueos: el juego espera al mezclador | Presente (cerrojo corto) | El cerrojo se toma por bloque de 64 muestras (`G/audio/sound_output_3ds.cpp:62-70`). El juego avisa y el mezclador le cede el turno (`G/audio/sound_native.cpp:50-52`, `sound_output_3ds.cpp:66`). No hay barrera de fotograma. Al acortar la mezcla, el bloque bajo el cerrojo dura menos. | Medio Old | — |
| B. Audio en otro núcleo | Presente | Hilo `audio_loop` (`G/platform/3ds/player_present.cpp:539-549`, bombeo cada 3 ms) en el núcleo 1, con prioridad del juego − 2 y pila de 64 KB (`:634-636`). Núcleo 0 si se pide el render en el núcleo 1 (opcional, `R/game_runtime.cpp:398-401`). | — | — |
| `APT_SetAppCpuTimeLimit` 80/70/60/50/40 con lectura | No aplica tal cual | Pide 30 % para el audio (`player_present.cpp:619`), y 80 % solo con el render en el núcleo 1. Comprueba el `Result` (`:620-624`). El informe lee el límite real del kernel (`cpu_nucleo1`, `R/game_runtime.cpp:1726`). La prueba 3 en consola (BUILD 128) demostró que dar el 80 % del núcleo 1 a la aplicación hace perder, porque GSP/FS comparten ese núcleo. | — | Alto si se cambia |
| Restaurar el límite al suspender o salir | No aplica | El hilo de audio queda inactivo con NDSP pausado; no hay evidencia de problemas. | Nulo | — |
| New 3DS: audio en el núcleo 2 | No aplicado (faltan datos New) | En New, el núcleo 2 es del hilo de render (`R/game_runtime.cpp:1161-1168`). Mover allí el audio le quitaría tiempo al render, y no hay volcados New que lo justifiquen. | ? / ? | Medio |
| `osSetSpeedupEnable(true)` | Presente | `G/platform/3ds/cpu_mode.h:9-15` (New, por defecto). | — | — |
| RSF `MaxCpu` | Presente | `packaging/sm64ds-integrated.rsf:42` = `0xD0`. `packaging/sm64ds.rsf:31` = `0x9E` es del visor histórico (`packaging/README.md`). | — | — |
| RSF `StoreProcessDataCache` (83) / `FlushProcessDataCache` (84) | 83 presente, 84 innecesaria | `sm64ds-integrated.rsf:95`. El código no llama a `svcFlushProcessDataCache`. | — | — |
| Flags de compilación | Presentes | `-march=armv6k -mtune=mpcore -mfloat-abi=hard -O2 -DNDEBUG`, modo ARM, sin LTO (`build3ds/cmake/CMakeFiles/sm64ds_sseq.dir/flags.make`). El compilador define `__ARM_ARCH_6K__`, `__ARM_FEATURE_DSP` y `__ARM_FEATURE_SIMD32`. No hay rutas que dependan de `__ARM_ARCH_6__`. | — | — |
| Divisiones en bucles calientes | Sin problema | A la frecuencia nativa el reloj de muestras usa desplazamientos (`G/audio/sample_clock.h:24-29`). En el ELF de BUILD 130 no hay llamadas a `__aeabi_*div` por muestra, píxel ni vértice; las que hay van por dibujo o por escena (`citro3d_backend.cpp:627` `count%3`, `ppu.cpp:1968`). Las demás son `vdiv` de VFP. | Bajo | — |
| C. Flush por kernel solo de lo escrito | Presente | GPU: `svcStoreProcessDataCache` con respaldo GSP y aviso en el log (`source/native_renderer/citro3d_backend.cpp:21-29`). PCM: `G/audio/sound_output_3ds.cpp:84-85`. Las llamadas a `GSPGPU_FlushDataCache` que quedan son del visor heredado (`viewer_3ds.cpp`, `gpu_composite.cpp`), que no se usa en la ruta nativa. | — | — |
| D. `C3D_FrameEnd(GX_CMDLIST_FLUSH)` | Presente | `source/native_renderer/citro3d_backend.cpp:827`. | — | — |
| D. Una sola espera por fotograma | Presente | `C3D_FRAME_SYNCDRAW` o cerca de presentación, nunca las dos (`citro3d_backend.cpp:172-176`). | — | — |
| D. Doble búfer de comandos | No justificado | `espera_gpu` media 3,95 ms en los volcados de BUILD 128 (`resumen.txt`). | Bajo | Medio |
| D. Vértices o skinning en la GPU | Fuera de alcance | `skin media=1.19 ms` de unos 20 ms de actualización (volcado BUILD 128). | Bajo | Alto |
| E/S por fotograma en el hilo del juego | Presente (resuelto en BUILD 130) | El sondeo de la tapa (PTMU) y el `fflush` del log van en hilos de baja prioridad (`R/test_mode.cpp:71-90`, `R/game_runtime.cpp:622`). Los informes se escriben fuera del bucle. | — | — |
| Telemetría por fotograma | Presente salvo audio | `cuadros.csv` y `resumen.txt` miden actualización, 2D, productores, dibujo, subida, envío y GPU. Faltaba el audio, ya añadido. | — | — |
| Mezclar directo al búfer NDSP | Descartado | Se mezcla en `mixer->pcm` y luego hay un `memcpy` de 256 B por cada 64 muestras (`G/audio/sseq_decoder.cpp:549`). Unicorn: unas 2 instrucciones por muestra, menos del 1 % del mezclador. Exigiría cambiar la API de `GenerateSamples`. | Nulo | Bajo |

## Cambios aplicados

| Archivo | Qué |
|---|---|
| `source/integrated_native/options/audio_mixer_kernels.hpp` (nuevo) | Bucles del mezclador. Versión portable (el bucle original) y versión ARMv6: `SMLAWB(g<<6, s, acc)` = `acc + ((s*g)>>10)` exacto; bucle de voz en ensamblador de 14 instrucciones por voz y muestra con `LDRD`/`STRD` sobre el acumulador alineado a 8 en la pila; `SSAT` para el recorte y `PKHBT` para empaquetar en estéreo. Solo se usa ARMv6 donde mide más rápido; las voces silenciadas, las de salida directa ch1/ch3, los bucles de menos de 8 muestras y `loopStart` > INT32_MAX van por la portable. `-DSM64DS_MIXER_PORTABLE` compila solo la portable. |
| `tools/prepare_integrated_mixer.py` (nuevo) | Parchea el `Player.cpp` generado (el proveedor fijado no se toca): llamadas a los bucles; borrado solo de las muestras del segmento (antes 3 KiB por llamada); reloj nativo sin aritmética de 64 bits por muestra; elección del bucle en `sound_output_3ds.cpp`, con `sdmc:/3ds/sm64ds/mezclador-portable.txt` o `SM64DS_MIXER_PORTABLE=1` para la portable. |
| `tools/prepare_integrated_audio.py` | Tiempos del bombeo, de la mezcla bajo el cerrojo y de la espera del mezclador (`sound_output_3ds.cpp`). Esperas del hilo del juego por el cerrojo (`sound_native.cpp`): si el cerrojo está libre lo toma sin anunciarse; si está ocupado, cronometra la espera. |
| `source/integrated_native/options/audio_diagnostics.hpp` | Contadores `Timing` atómicos de 32 bits en µs. |
| `R/game_runtime.cpp`, `R/test_mode.cpp`, `R/test_mode.h` | Sección `== audio ==` en `resumen.txt`, medida en la misma ventana que la situación, y `audio=X%` en `resultado.txt`. |
| `tools/test_mode_report.py` | Columnas `audio%`, `mezcla_ms_s` y `espera_juego_max`. |
| `tools/prepare_integrated_render60.py`, `tools/build_integrated_native.py` | Encadena el nuevo preparador y lo añade a la evidencia. |
| `tools/package_native_renderer_hardware.py` | Acepta una etiqueta experimental opcional (`N SESION TAG`). |
| `tools/test_integrated_mixer_armv6.py` (nuevo) | Prueba bit a bit en Unicorn (ver «Pruebas»). |
| `tests/integrated_audio_save/test_mixer_lock.cpp` (nuevo), `run.py`, `3ds.h`, `test_output.cpp` | Prueba de hilos del cerrojo juego/mezclador con TSan; simulador de `svcGetSystemTick`. |

Árbol generado (`game-runtime-20261026-r1`): `Player.cpp`, `sound_output_3ds.cpp`,
`sound_native.cpp`, `audio_diagnostics.hpp` y `audio_mixer_kernels.hpp` se
regeneraron con los preparadores desde el proveedor fijado. Antes se comprobó que,
sin los parches nuevos, la cadena reproduce byte a byte los archivos anteriores.
Sus SHA-256 están en `native-renderer-overlay.json`.

## Pruebas

**Bit a bit en ARM real (Unicorn, `UC_CPU_ARM_11MPCORE`, VFP por CPACR/FPEXC):**
`build/python/bin/python tools/test_integrated_mixer_armv6.py --sdat <sound_data.sdat> --kernel-cases 8000`

- Se compilan con devkitARM y los flags del juego el `Player.cpp` fijado, el
  parcheado ARMv6 y el parcheado portable. Se comparan el PCM y todo el estado
  del reproductor tras **cada** llamada.
- Mezclador completo con 15 secuencias del juego, perturbaciones aleatorias y
  extremas, 4 frecuencias (32728, 32768, 48000, 22050), 3 interpolaciones,
  bloques de 1 a 512 muestras y desplazamientos de salida desalineados. Las
  perturbaciones cubren volumen, pan, recarga y contador del temporizador,
  retardo inicial, `loopStart`, fin de one-shot, silencios, canales excluidos,
  salidas directas ch1/ch3, surround, canales PCM directos y unidades de
  captura. Resultado: **6.300 llamadas × 3 versiones, idénticas**.
- Bucle de voz: **8.000 casos** aleatorios y extremos (muestras ±32768, ganancia
  máxima, `pos` negativa, fin a mitad de bloque, bucles de 1 muestra, datos con
  dirección no múltiplo de 4, ciclos no uniformes). **Idénticos.**
- Recorte y empaquetado: 600 casos con sumas extremas y destino desalineado. **Idénticos.**

**Instrucciones ARM ejecutadas** (hook de bloques de Unicorn):

| Medida | Original | ARMv6 | |
|---|---|---|---|
| Mezclador completo, 10 músicas del juego, 64 muestras por llamada | 599 por muestra | 228 por muestra | **2,63×** |
| Bucle de voz a la frecuencia nativa, 1 paso por muestra | 58 por voz y muestra | 14 | 4,14× |
| Ídem, 2 pasos por muestra / 1 paso cada 2 | 64 / 53 | 19 / 12 | 3,37× / 4,42× |

Equivalencia aproximada (1 instrucción por ciclo, sin caché): con la misma
música, el mezclador pasa del 7,3 % al 2,8 % de un núcleo de 268 MHz.
El ciclo real del ARM11 será peor en los dos casos; por eso hay que medir en
consola.

**Proyecto:**

- CTest host: 103/104. Falla `native_background_preparation`, como antes, porque
  el `python3` del sistema no tiene `ndspy`.
- `tests/integrated_audio_save/run.py`: PASS, incluidas `ndsp-output` y la nueva
  `mixer-lock`. Esta última comprueba con TSan la exclusión mutua durante
  3000+3000 secciones, la recursión, el aviso del juego, las esperas
  cronometradas y que no hay bloqueo (vigilante de 20 s).
- `tests/test_build_tools.py`: OK.

## Build experimental

- `build/native-renderer/playable-r1/build_experimental.sh armv6` genera
  `game-p130-armv6/`. `build_experimental.sh --restore` devuelve la cabecera
  generada a la oficial.
- El binario contiene `sm64ds_mixer::mix_voice_armv6<true|false>` con
  `SMLAWB`, `LDRD`/`STRD`, `ADDS`/`BPL`, además de `SSAT`/`PKHBT` en
  `SoundPool::GenerateSamples`, la cadena `Native sound mixer: kernel=%s` y la
  etiqueta `BUILD 130-armv6`.
- Rutas y SHA-256: ver «Artefactos» al final.

## Qué medir en consola (antes y después, Old y New)

Usar el mismo recorrido del modo prueba (SELECT+START).

1. **Antes:** los volcados de BUILD 130. Sus `resumen.txt` no tienen sección de
   audio; se compara `fps`, `p95`, `juego`, `render` y `gpu`, y la línea
   `audioLoad=` de `native-test.log` al salir.
2. **Después, A/B dentro de la misma CIA 130-armv6:**
   - prueba con el bucle ARMv6, que es el normal;
   - prueba con `sdmc:/3ds/sm64ds/mezclador-portable.txt` (archivo vacío), que
     usa el bucle portable. El sonido es el mismo bit a bit; solo cambia el tiempo.
   - El log confirma cuál se usó: `Native sound mixer: kernel=armv6|portable`.

| Columna (`resumen.txt` → `== audio ==`) | Qué es | Esperado Old | Esperado New |
|---|---|---|---|
| `mezcla_ms_por_s` | CPU del mezclador y secuenciador por segundo (tiempo de pared bajo el cerrojo) | Bajar ~2× con ARMv6 frente a portable | Bajar ~2× (base menor a 804 MHz) |
| `bombeo_ms_por_s` / `carga_audio` | Hilo de audio entero (mezcla, esperas y NDSP) | Bajar | Bajar |
| `bloque_max_ms` | Peor bloque de 64 muestras bajo el cerrojo | Bajar | Bajar |
| `espera_audio_ms_por_s` | El mezclador cede el cerrojo al juego | Igual o menor | Igual |
| `juego_espera_ms total/max/esperas` | **El hilo del juego esperando al mezclador** | Bajar (bloques más cortos) | Bajar |
| `colas_vacias` | NDSP se quedó sin búfer (posible corte) | 0 o menos que antes | 0 |
| `fps`, `p95`, `juego`, `gpu` | Efecto en el juego | Con el audio en el núcleo 1 (por defecto), efecto indirecto: más tiempo del núcleo 1 para GSP/FS y menos espera del juego. Con `nucleo-render-1.txt` el mezclador está en el núcleo 0 y debería notarse directamente en `juego`. | Pequeño |
| `audio=X%` en `resultado.txt` | Resumen por situación | — | — |

`tools/test_mode_report.py` ya saca las columnas `audio%`, `mezcla_ms_s` y
`espera_juego_max`.

## Descartado

- Mezclar directo al búfer NDSP: menos del 1 %, medido.
- Audio en el núcleo 2 de New: no hay datos New, y ese núcleo es del render.
- Pedir 80…40 % del núcleo 1: la consola ya demostró que es peor en este port.
- `FlushProcessDataCache` en el RSF: no se usa.
- Doble búfer de comandos y skinning en GPU: la telemetría no lo justifica.
- `-O3`: no se probó. El bucle caliente ya está en ensamblador, así que no
  ganaría nada medible ahí.

## Cómo revertir

- Quitar la llamada a `prepare_mixer` en `tools/prepare_integrated_render60.py`
  y regenerar `Player.cpp` y `sound_output_3ds.cpp` desde el proveedor.
- Comparar con `git diff perf/armv6-2026-11-09~1 perf/armv6-2026-11-09`.
- En consola, sin recompilar: crear `sdmc:/3ds/sm64ds/mezclador-portable.txt`.

## Azahar (prueba de humo, no es rendimiento)

Una sola instancia cada vez, cerrada al terminar. Old 3DS, jardines del castillo.

- `.3dsx`: arranca con `BUILD 130-armv6` y `Native sound mixer: kernel=armv6`.
  Hace 3 de 3 informes con la sección `== audio ==` y `colas_vacias=0`, y las dos
  pantallas se ven bien.
- `.3dsx` con `SM64DS_MIXER_PORTABLE=1`: `kernel=portable` y `mezclador=portable`
  en el informe. El interruptor funciona.
- CIA instalada: `kernel=armv6`, 20 de 20 informes, `Native game exit code: 0`.

Los milisegundos de audio en Azahar son tiempo del Mac emulando, no del ARM11.

## Artefactos

| Archivo | SHA-256 |
|---|---|
| `build/native-renderer/playable-r1/hardware/build130-armv6/SM64DS-Native-BUILD130-armv6.cia` | `a7b4ff5ddc07da114b556b7e0303b329da74ba91a4faa3529bea558ecab26a3b` |
| `build/native-renderer/playable-r1/hardware/build130-armv6/sm64ds.elf` | `fe0bd593593cfb790233f3b8c47c75261d4499d09f466be6948a6243d2c34996` |
| `build/native-renderer/playable-r1/game-p130-armv6/sm64ds.3dsx` | `bd9a390aad5c8016af70c8beacb22b127523a142b79a0c395b6d7e664b2f8150` |
| `build/native-renderer/playable-r1/game-p130-armv6/sm64ds.elf` | `6c60c1e0ebfe910d8ce8d764e8af1ced71ed9ec4d1b35b0a186fb35cbdd96e9f` |
| `build/native-renderer/game-runtime-20261026-r1/source/port/third_party/sseqplayer/Player.cpp` | `f386fd0db392c869b00b895f7f1889dcf45da1e620d8588e70445b1e883422dd` |
| `build/native-renderer/game-runtime-20261026-r1/source/port/audio/sound_output_3ds.cpp` | `0887bba928795d98a451917ddb76f6c849586f8315f619d986513f24d5d15424` |
| `build/native-renderer/game-runtime-20261026-r1/source/port/audio/sound_native.cpp` | `6437e89c749e518ebd40d980150489ad5be430ff1210cd773fc5f568ec898a2e` |
| `build/native-renderer/game-runtime-20261026-r1/source/port/platform/3ds/audio_mixer_kernels.hpp` | `e0ac7ac8b44a031c8470ce781f4f4575b363b59bb3a66a0de263a867f2e8f961` |
| `build/native-renderer/game-runtime-20261026-r1/source/port/platform/3ds/audio_diagnostics.hpp` | `4456906c23d6fc1c608fd9d6b88163762220a6d65f7786d233164934389b61e1` |
| `source/integrated_native/options/audio_mixer_kernels.hpp` | `e0ac7ac8b44a031c8470ce781f4f4575b363b59bb3a66a0de263a867f2e8f961` |
| `source/integrated_native/options/audio_diagnostics.hpp` | `4456906c23d6fc1c608fd9d6b88163762220a6d65f7786d233164934389b61e1` |
| `source/integrated_native/renderer/game_runtime.cpp` | `e65966b2a0020442b0b7bf258026f2bae7cddd151fbddbda51ca97ff8aefb75f` |
| `source/integrated_native/renderer/test_mode.cpp` | `2ea03e9c388b1ec6132bb6545b4ba88633d1c878b16c2c7df3928e4128c1d4ca` |
| `source/integrated_native/renderer/test_mode.h` | `eda70bcfd94f5d83dcccca52441ec71a95c3cc5a03ae66c2d3c7f2b81aa5aee8` |
| `tools/prepare_integrated_mixer.py` | `9feff1fa6639e52edb55f554bbc2538a3845c0dec02b3448b5c00262b648a590` |
| `tools/prepare_integrated_audio.py` | `faaa3a7c7c3721c46dd31086dfd4a0b2925f51fd28f16b29fad06aa87ffc8b8c` |
| `tools/prepare_integrated_render60.py` | `7241341c495cd9b60428da49882da341459957dcf43fa799ef8b0ad5381b26dc` |
| `tools/build_integrated_native.py` | `f30f8345be7d0fcfe626380f9960a05ebaacf308f60546eecabc7662fe96ca51` |
| `tools/package_native_renderer_hardware.py` | `eca10b396ba2ad9cea095212647ce31a07dbbc5d6a73c30b24da511cf72272e5` |
| `tools/test_integrated_mixer_armv6.py` | `19d82313378b2d40099195c5903504938224d84eeb2a6044820de5d77eb23fc8` |
| `tools/test_mode_report.py` | `ae0874b9bebcbac7be94d43989a454e1e7575450b48b91fe24a06e29bedb4da9` |
| `tests/integrated_audio_save/test_mixer_lock.cpp` | `553847ab0e180f9e79e89c55034f4f77e2f027ce06051c89e8c0d37bb57999b8` |
| `tests/integrated_audio_save/run.py` | `a6c557b092a073c19514663b52f742dd0fca0e33b8650fe7b88463afb5b57afd` |
| `tests/integrated_audio_save/3ds.h` | `5b96113e3cf9b09211bc1db2d1ef660dd96a700aa205d72e9f172a6bd426f588` |
| `tests/integrated_audio_save/test_output.cpp` | `b920dde4ac87a99bc8a0ac240b3d099c7cdefdcb05fb3386743e2c577c55326d` |
