# Investigación de otros ports DS y SM64DS — 2026-11-17 (base BUILD 136)

Petición: buscar en otros ports de DS y del SM64DS soluciones concretas para
los problemas abiertos. Ya estudiados antes y no repetidos aquí: SoH-3DS,
sm64 de mkst, OpenLara, gdx-3ds, MKW-3DS/NP49, MK64-3DS y sm64ds-enhanced
(revisiones del 31-10 y 07-11 en [3DS_PORT_RENDER_RESEARCH.md](3DS_PORT_RENDER_RESEARCH.md);
de ahí salieron los dry ticks y la división/raíz por VFP; su list memo e
interpolación sobre GX no aplican porque el modo nativo descarta el FIFO GX).

Esto es lectura de código ajeno, no medición. Ninguna técnica está probada
en nuestra consola. Clones de trabajo (temporales): `/tmp/sm64ds-research`,
`/tmp/dsresearch`, `/tmp/r3ds_src`.

## 1. Audio: chasquidos y costo del mezclador

Comprobado en nuestro código:

- **`colas_vacias` no ve los huecos.** `queue_drained`
  (`source/integrated_native/options/audio_diagnostics.hpp`) solo cuenta si
  *todos* los búferes estaban terminados al empezar `pump()`. Un búfer que
  termina mientras se mezcla otro lo recupera el bucle de reposición
  (`tools/prepare_integrated_audio.py`, «catch-up») sin contarlo. En
  `dumps/prueba133` (BUILD 133, Old) `bloque_max_ms` supera 15,6 ms en 44
  informes y llega a 76,11 ms (`07-fortaleza-whomp/31-zona3-correr`) con una
  cola de 3×512 muestras ≈ 47 ms, y aun así `colas_vacias=0` en todos.
- La cola sigue en 3×512 (`third_party/sm64ds-3ds/port/audio/sound_output_3ds.h`),
  igual que el port de Breton.

Fuera:

- libctru `ndsp.c`: `ndspChnIsPlaying()==false` antes de reponer indica un
  hueco real; `ndspGetDroppedFrames()` da las tramas perdidas por el DSP.
- 3dbrew [KResourceLimit](https://www.3dbrew.org/wiki/KResourceLimit): con el
  30 % el núcleo 1 corre 0,6 ms de cada 2 ms. Un cerrojo tomado durante el
  apagón de 1,4 ms bloquea a quien lo espere.
- Colas mayores: sm64ds-enhanced 4×1024 (125 ms); RetroArch
  `ctr_dsp_audio.c` (b90d36a) y blargSnes `audio.c` (f3ab5a9) usan un anillo
  en bucle y mezclan exactamente lo que el DSP consumió (`ndspChnGetSamplePos`).
  Nuestro propio zelda-tmc-3ds (`port_audio_3ds.c` ae25472) movió el worker al
  núcleo 0 porque «una cola más larga no compensa un worker sin cuota».
- Voces en NDSP (una por canal DS): posible pero no exacto. Actualización por
  trama DSP de 160 muestras ≈ 4,89 ms (`ndsp.c`), cerca del tick DS de 5,21 ms
  (fincs/SSEQPlayer `sndbase.arm.c`). Solo PCM8/PCM16/DSP-ADPCM; el
  IMA-ADPCM ya lo decodificamos. Límite de `rate` sin documentar (TMC oyó
  basura a ~16×). Perdería la puerta bit a bit de Unicorn; captura y salidas
  directas ch1/ch3 seguirían en software.

## 2. 2D: puente PPU

Lo que ya existe: `ppu_prepared_inputs_unchanged` compara con `memcmp` una
copia de todos los tramos de VRAM usados y los registros de mezcla; la capa
OBJ de la ruta CPU es incremental; mapa afín en GPU; subidas por bandas con
`memcmp` contra una copia de 196 KiB.

Nuevo de otros ports:

- **GabeSud0/pokeplatinum-3ds** (`c84883d652`,
  `nitro/src/port_render2d.c` `NitroPort_Render2DSplitIfChanged`): primero una
  firma pequeña (registros, paletas, OAM); solo si coincide, hash de la VRAM
  realmente visible; si solo cambió OAM, solo se reconstruyen sprites.
  Subida en bandas de 4 filas. Antes de esto un redibujado completo costaba
  12–25 ms en su Ruta 206. Para nosotros: una firma barata **antes** del
  `memcmp` de todos los tramos.
- **AKCore/pokeplatinum-port** (`3357155cef`, `3ds/src/3ds_gpu2d.c`,
  `3ds_tile.c`, `3ds_gpu.c`): BG y OBJ como quads PICA desde un atlas de tiles
  1024×512 (clave: dirección + contenido de paleta + bpp; los flips son UV).
  Prioridad por orden de dibujo, ventanas en stencil, alfa con un bit de
  stencil y mezcla constante, BLDY/MASTER_BRIGHT dentro de la paleta. Lo no
  admitido (BG afín, OBJ bitmap/mosaico/semitransparente) cae a la CPU con
  contador por causa. Su `perf-report.txt` no prueba velocidad (167–423 ms,
  probablemente emulador).
- **melonDS** (`497aa6c`, `src/GPU2D_OpenGL.cpp`, PR #2553): suciedad de VRAM
  por banco en bloques de 512 B; sprites en atlas, profundidad =
  prioridad×128 + índice OAM. Su paleta en fragment shader no existe en PICA.
- No se encontró ningún reemplazo semántico de NNS_G2d.

## 3. Interpolación a 60 FPS

Comprobado en nuestro código (`source/native_renderer/scene_interpolation.hpp`):
`PoseHistory::midpoint` mezcla al 50 % las matrices `object_to_clip`
(cámara incluida) y la continuidad solo exige misma escena y serie+1. No hay
marca de corte de cámara, de teletransporte ni de aparición, ni
renormalización de la rotación.

- **sm64ex / sm64coopdx** (`8cd6e59`, `rendering_graph_node.c`): un nodo
  interpola solo si se dibujó el tick anterior y no lleva sello de salto;
  `skip_camera_interpolation()` en ~14 sitios (warps, `level_update.c:704`).
  Mezcla en espacio de mundo y aplica después la cámara interpolada. Sombras
  reconstruidas en la posición interpolada.
- **Tango, ramas `release/game-*`** (hasta `release/game-1.0.7`, `b4ab5d791`;
  «SmoothMotion», `520765f46`, `port/ntr/gx.cpp`): rechaza pares si cambia la
  forma de los comandos de matriz o hay teletransporte. Casos especiales:
  parpadeo con el tick N-2 (`478a2a6c3`), malla del fundido fija en pantalla,
  que evita la imagen negra tras caer a un pozo (`576814f9a`), scroll de
  textura a través del wrap (`98c910a70`), diálogos (`ee61ffdd7`), partículas
  por elemento (`8380d6aff`, `0fdef81ce`), HUD y medidor (`2005d96cc`,
  `0cb0f51fd`).
- **sm64ds-enhanced** `interp3ds.cpp`: renormaliza las filas de rotación tras
  el lerp; repite la imagen si menos del 85 % de los vértices se emparejan o
  si hay corte de cámara o teletransporte.
- **Zelda64Recomp** `camera_transform_tagging.c`: detector de corte por
  velocidad predicha, mejor que umbrales fijos.
- BCA (SM64DSe `bca_format.txt`): con la bandera `AA` los fotogramas impares
  son el punto medio de sus vecinos; muestrear la animación a medio paso daría
  la pose intermedia sin encoger rotaciones. Requiere acceso a la lógica de
  animación: idea, no plan.

## 4. Render (DS GX en PICA)

Comprobado: los materiales (`game_material.cpp`) leen los bits 11 y 15 de
POLYGON_ATTR pero no el **bit 14 (prueba de profundidad igual)**; `GPU_EQUAL`
solo se usa para `Blend::highlight` y las sombras. Tango lo corrigió en
`00b737736` (anillos de onda de Bowser). La niebla sigue como experimento
(`scene_renderer.cpp:847`); Tango corrigió la tabla (`e9673bc0c`) y el orden
de canales del color (`fdb38940c`, agua roja).

- Polygon ID y translúcidos (melonDS `GPU3D_OpenGL.cpp` L990-1085): todo en
  stencil de 8 bits (ID en bits 0-5, 0x40 translúcido, 0xFE máscara de
  sombra). Es la vía para las políticas que `SceneRenderer` hoy rechaza
  (`polygon_id!=255`).
- Igualdad con tolerancia ±255/2^24 (DeSmuME `render3D.h:28`).
- W-buffer: PICA lo tiene (`C3D_DepthMap(false,…)`); nosotros fijamos
  `C3D_DepthMap(true,-1,0)` (`citro3d_backend.cpp:722`). Solo importa si una
  captura muestra el modo W activo.
- Toon por píxel: rojo como coordenada de textura en una textura toon con TEV
  (igual que la textura 1D de DeSmuME). Nuestro `use_toon` es por vértice.
- PICA: citro3d borra la caché de vértices y reinicia la primitiva en cada
  dibujo (`drawElements.c`); los uniformes contiguos se escriben en un solo
  comando (`uniforms.c`). El autor de un port de F-Zero X afirma 11 → 0,7 µs
  por dibujo en Old al saltarse citro3d, sin código publicado. citro3d #50: el
  tiempo GPU incluye la transferencia a la LCD.

## 5. SM64DS: estado de las otras bases

- Tango: el pin `6230b29` es solo un cambio de versión; el linaje real son las
  ramas `release/game-*` (8.396 commits más). `main` dice 100 % emparejado. El
  port de PC es solo Windows (D3D11); ninguna rama tiene trabajo 3DS.
- Breton: sin commits tras el pin `88725627c`. Issue #3 abierta: «Save and
  Continue/Save and Quit» no guarda estrellas en New 3DS XL. Hay que
  comprobarlo en el nuestro.
- celsowm/sm64sd-saturn `tools/sm64ds_pipeline/`: conversor Python de
  BMD/BCA/texturas (4×4 incluidas)/KCL, útil como segunda referencia para los
  `modelos_faltantes`. SM64DSe `Documentation/*_format.txt` documenta
  BMD/BCA/BTP; no existe BTA (la animación SRT de nivel está en el overlay;
  `LevelTexAnim` en SM64DSe-Ultimate).

## Orden propuesto

1. Arreglar `colas_vacias` (`ndspChnIsPlaying` y `ndspGetDroppedFrames`) y
   medir en consola antes de tocar el audio; después, una cola mayor o un
   anillo y quitar el cerrojo mientras se mezcla.
2. Interpolación: sello de corte de cámara/warp y de aparición, mezcla en
   mundo con renormalización y regla de repetición; casos de fundido y HUD.
3. Bit 14 de POLYGON_ATTR (igualdad de profundidad) y revisión de niebla con
   los arreglos de Tango.
4. 2D: firma barata antes del `memcmp`, reconstrucción solo de sprites y,
   como paso grande, OBJ/BG como quads PICA con caída a CPU contada.
5. Comprobar el bug de guardado de la issue #3 de Breton.
