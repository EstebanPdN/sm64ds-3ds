# Current acceptance checklist

## BUILD142 — perfiles Old 3DS y New 3DS, 2026-11-20

New: heap lineal de 32 MiB en el modo de 124 MB, presupuesto de GPU de
20 MiB, VRAM para el mapa de abajo (un plano del HUD cede la suya) y
escrituras a la SD en el núcleo 1. Old: el mapa de la intro (nubes que se
desplazan y ~40 sprites de nubes) cuesta ~3 veces menos y una imagen
retenida ya no obliga a redibujar el plano entero.

Comprobado (Azahar integrado; no es evidencia de consola):

- [x] CIA New 100 %, Lock 60: recorrido COMPLETO 9/9, 0 fallos del mapa
  (BUILD 141: 8, todo el 2D de la intro en la CPU); castillo 37,5 -> 58,9
  FPS, jardines 41,6 -> 59,0.
- [x] CIA Old 25 %, Lock 60: intro 16,5 -> 23,1 FPS de media (ventanas del
  log); jardines 28,3 -> 28,7.
- [x] CIA Old 50 %, Lock 60 y Unlocked: 9/9, 0 fallos; capturas iguales a
  BUILD 141 salvo el número de build (y el paso 5, que se toma con la intro
  en marcha y varía entre ejecuciones también en BUILD 141).
- [x] GPU: Old 9/9 pasos sin estados nuevos (prueba133 + BUILD 139); New sin
  estados que BUILD 141 New no usara.
- [x] CTest 113/113; test del mapa (162 publicaciones, scroll y copia
  retenida); test de recortes de la PPU 4/4 (también contra BUILD 133).
- [ ] Consola: intro con Lock 30/60/Unlocked; New 3DS si hay una.
- Pendiente: selección de partida en Old (lógica del juego + malla, ~15 ms
  por actualización al 50 %) y la carta de Peach (carga y 2D de arriba).
- CIA SHA-256 `59ed9612b952d86dd368956e1ff6fae01b381b48f0bde769533aa53603954ba1`
  (53.187.520 bytes), QR en la red local.

## BUILD141 — pantalla de abajo de la intro en la GPU, 2026-11-18

Las capas de nubes (BG0/BG1) y la niebla de la torre (BG2) las dibuja la GPU
con su desplazamiento; los sprites van en un plano detrás de las capas y otro
delante. Mismos estados de GPU que el mapa giratorio.

Comprobado (Azahar integrado; no es evidencia de consola):

- [x] .3dsx Old 100 %, Lock 60: recorrido COMPLETO 9/9, 0 cargas fallidas,
  0 fallos del mapa; capturas idénticas a BUILD 140 (salvo el texto de la
  versión); GPU: 9/9 pasos sin estados nuevos (prueba133 + BUILD 139).
- [x] CIA (80 MB) Old 50 %, Lock 60, ventanas de la intro: casi todas 35-57
  FPS (BUILD 140: 26-33); el tramo más pesado 25-30 como antes.
- [x] Test de referencia: 62 publicaciones con capas iguales a la PPU de CPU
  (máx. 3/255 en píxeles mezclados); imagen retenida sin fantasmas.
- [x] CTest 113/113; audio/guardado PASS.
- [ ] Consola: FPS de la intro con Lock 30/60/Unlocked.
- [ ] Aparte (ya en BUILD 140): en modo New 3DS el mapa de abajo falla al
  publicarse ("allocation failed") y cae al camino de CPU.
- CIA SHA-256 `5d1df8d80915ef94990686699c95a0868826642ec8fed035b67b26026d4044f2`
  (53.179.328 bytes), QR en la red local.

## BUILD140 — fallos del informe de consola de BUILD 139, 2026-11-18

Arreglos: jardines sin terreno ni cielo y Luigi sin cuerpo (memoria agotada
por una segunda vía de caché de la PPU que nunca se liberaba), sombra sobre
el cuerpo de Yoshi (regla del DS: nada con polygon ID 1 recibe sombra),
Lock 60/Unlocked clavado en 29,9 (CPU y espera de GPU/LCD contadas aparte;
la cola de la segunda imagen ya no tira la siguiente actualización),
estrella del título cargada antes de empezar, transición tras File 1 desde
blanco, niebla de la torre y sprites translúcidos mezclados por la GPU, y
volcados comprimidos (SM64VMZ1, LZ4).

Comprobado (Azahar integrado; no es evidencia de consola ni de FPS reales):

- [x] .3dsx, Old al 100 %, Lock 60: recorrido COMPLETO 9/9, 0 cuelgues,
  0 cargas fallidas; capturas idénticas a BUILD 140 sin el paso de la niebla
  salvo la pantalla de abajo de la torre (máx. 3/255).
- [x] CIA (80 MB), Old al 25 %, Lock 60: recorrido COMPLETO 9/9, 0 cargas
  fallidas; torre de la intro 17,6 FPS (BUILD 139: 15,3; BUILD 137: 8,3);
  estrella visible durante el fundido del título.
- [x] CIA al 25 %, Unlocked: COMPLETO 9/9; pares interpolados en los jardines
  (39 al caminar) sin segunda imagen perdida.
- [x] Registros al salir del menú nativo: BLDCNT 00bf/00bf, BLDY 16/16.
- [x] Volcado manual (L+R+A): memory.vm de 8,5 MB (antes 90 MB), 2,4 s;
  se expande byte a byte; memoria lineal recuperada; el juego sigue.
- [x] GPU: 9/9 pasos sin estados nuevos frente a prueba133 + las listas de
  BUILD 139 (que ya corrió esas pantallas en la consola).
- [x] CTest 113/113 (recortes exactos sin la mezcla por GPU; con ella,
  17,8 M píxeles dentro de 3/255); audio/guardado PASS.
- [ ] Consola: jardines con Lock 60 (se esperan ~45-55 FPS), torre de la
  intro (~22 FPS esperados), terreno/cielo/Luigi, sombra de Yoshi nadando.
- [ ] Pendiente: los jardines de la intro siguen limitados por el mapa de
  abajo (nubes que se desplazan, ~22 FPS); el arreglo completo es dibujar
  las capas de nubes en la GPU (paso 2).
- CIA SHA-256 `dfad62f76e38eebc7ec9587e458265979da76811645261db8070b01eb3327958`
  (53.175.232 bytes), QR en la red local.

## BUILD139 — fallos del informe de consola de BUILD 138, 2026-11-18

Arreglos: orden de Opciones (OK/Atrás vuelven a la lista), transiciones sin
hueco entre imágenes de opciones, volcados que congelan todo (también la
música) y vigilante con plazo por progreso, sombras que ya no oscurecen a su
dueño (bit 7 del stencil), guardado y ajustes en segundo plano, mapa de la
intro a 10 Hz cuando es caro y estimación de la segunda imagen de 60/Unlocked
que ya no se queda obsoleta.

Comprobado (Azahar integrado; no es evidencia de consola ni de FPS reales):

- [x] CIA, Old al 25 %, Lock 60: recorrido COMPLETO 9/9, 0 cuelgues; intro
  8,3 → 15,3 FPS. .3dsx Unlocked al 25 %: imágenes intermedias de nuevo (0 →
  13 en un tramo). .3dsx Lock 60 al 100 %: 58-60 FPS en el juego.
- [x] Sonda de guardado (SM64DS_DEV_SAVE_PROBE=1): tres guardados seguidos,
  la versión más nueva en la SD, sin .tmp/.bak sobrantes.
- [x] Opciones: Display → OK vuelve a Opciones con Display seleccionado.
- [x] Sombra en el suelo y zapatos de Yoshi sin oscurecer (capturas).
- [x] CTest 112/112; audio/guardado 8 PASS.
- [ ] Consola: estados de stencil nuevos (0x105/0x106) nunca ejecutados en la
  Old; intro, 60/Unlocked y guardado tras la llave.
- CIA SHA-256 `397eb3111100d5b737f70ed71c38aff17007199ed1f4df0ac3b8afa259c8adf3`
  (53.158.848 bytes), QR en la red local.

## BUILD138 — cuelgue de GPU en la selección de partida (Old), 2026-11-18

Informe de la consola con BUILD 137 (`dumps/prueba137`, Lock FPS 60): «la GPU
no responde» en la primera imagen de la selección de partida. Solo la malla
ondulada de la selección usaba las ramas gris y toon del shader de
iluminación, que la consola nunca había ejecutado (0 de 10.709 dibujos de
`dumps/prueba133`). La malla se ilumina ahora en la CPU con la misma
aritmética (`PreparedVertexLighting`) y se dibuja por la ruta sin luz.

Comprobado (Azahar integrado; no es evidencia de consola):

- [x] Selección de partida: imagen idéntica byte a byte (solo cambia el número
  de BUILD); todos los dibujos usan ramas ya ejecutadas en la consola
  (`tools/check_pica_hardware.py`). Todos los pasos del recorrido, también.
- [x] Recorrido de jugador .3dsx con Lock 60 (ajustes de la consola): COMPLETO,
  9/9, 0 cuelgues, 168 s.
- [x] CTest 112/112 (nueva: `native_check_pica_hardware`).
- [ ] Recorrido de la CIA al 25 % con Lock 60: cortado al empezar porque otra
  sesión de Code Atlas abrió su juego en el Azahar compartido; la CIA arrancó
  en modo 80 MB.
- CIA SHA-256 `b25d9ac071a600d2069736cf7daa0f29552728f0e05e44d9400f1aea6deebda2`
  (53.158.848 bytes), entregada por QR en la red local.

Pendiente en consola: Adventure → selección → partida con Lock 30, 60 y
Unlocked; música en el menú.

## BUILD137 — cuelgue de Adventure en la Old, menú, música, vigilante y recorrido de jugador, 2026-11-17

Causa del cuelgue de Adventure (pantalla blanca, música sonando, HOME y POWER
muertos; abierto desde BUILD 52): en la Old el hilo del juego y el anfitrión
(0x30) comparten el núcleo 0 con el hilo de render y con el hilo de APT de
libctru (los dos a 0x31). La actualización de la selección de partida no cabe
en su tiempo en una Old real, la línea de tiempo fija nunca deja dormir al
juego y los dos hilos de 0x31 no vuelven a ejecutarse: no hay imagen nueva y
HOME/POWER no se atienden. Reproducido en Azahar con la CPU al 25 % (0,2 FPS
en la selección; el log tampoco se escribía).

Arreglos: juego a 0x32 (bajo APT), espera acotada al render tras 3
actualizaciones sin imagen con la línea de tiempo aplazada, esperas de HOME,
cierre y GPU acotadas (`--wrap=gxCmdQueueWait`, GPU marcada inutilizable si no
responde), log con búfer desde la primera actualización, productores del menú
fuera de las actualizaciones descartadas, malla de la selección convertida una
vez por punto, mezclador al 50 % del núcleo 1 y cola de 4 búferes, contador de
vaciados reales, vigilante de cuelgues y recorrido de jugador
(L+SELECT+START). `SetThreadPriority` añadido al RSF.

Comprobado (Azahar integrado de Code Atlas; no es evidencia de consola ni de FPS):

- [x] CPU al 25 % (Old): la selección de partida pasa de 0,2 a 7-13 FPS; el
  recorrido de jugador completo (título → puerta del castillo) termina:
  9/9 informes, 0 cuelgues, 181 s (repetido tras la revisión: igual, 8 FPS en
  la selección).
- [x] Old al 100 %: recorrido COMPLETO, 9/9, 0 cuelgues, 164 s; New al 100 %:
  COMPLETO, 9/9, 0 cuelgues, 162 s.
- [x] Bob-omb (12 situaciones, Old): 24/24 capturas idénticas byte a byte a
  BUILD 136.
- [x] Vigilante: render colgado a propósito → `cuelgue-01.txt` (render 16 s en
  «espera_GPU»), log pendiente y 19 MB de memoria; juego colgado a propósito →
  «el juego no avanza» a los 15 s.
- [x] CIA final (modo 80 MB, Old al 100 %, núcleo 1 al 50 % concedido):
  recorrido COMPLETO, 9/9, 0 cuelgues, 0 vaciados de audio, 163 s. SHA-256
  `b892a4773a1be468a0c599b81e36747922d3587829828ebfe742224c1fb71432`
  (53.154.752 bytes; ELF verificado byte a byte), entregada por QR en la red
  local.
- [x] Revisión adversarial (10 revisores): aplicados todos los hallazgos
  confirmados o plausibles salvo dos, entre ellos la espera GX acotada por progreso de la
  cola (no por tiempo), la pausa del proceso, el plazo de 60 s de los
  informes, el cierre por APT antes de `svcExitProcess`, el motivo «la GPU no
  responde» y que el propio informe del vigilante no cuente como pausa.
  Quedan sin aplicar dos de prioridad baja: distinguir el hilo anfitrión del
  del juego en los informes, y vigilar el resto de la transición a HOME
  después de nuestro gancho.
- [x] Pruebas de ordenador: 111/111 de CTest (nuevas: recorrido con cuatro
  ángulos de cámara y puerta abierta/cerrada; malla de la selección idéntica a
  la anterior en 200 mallas aleatorias), herramientas y audio/guardado (cola de
  4 búferes y vaciados reales).

Pendiente en consola (Old):

- [ ] Adventure → selección de partida sin pantalla blanca; HOME y POWER
  responden siempre (también durante cargas).
- [ ] Música sin cortes en menús (`audioUnderrunsTotal` en el log; A/B con
  `audio-cpu.txt` 30/50/80).
- [ ] FPS del menú y de la selección en la Old real (los informes del recorrido
  de jugador los miden).
- [ ] Cierre de emergencia del vigilante con HOME (Azahar no envía HOME).

## BUILD136 — BUILD 135 recompilada desde el código publicado, 2026-11-15

Primera versión compilada desde el código subido a GitHub (rama
`native-renderer-test-line`). Respecto a BUILD 135 no cambia el
comportamiento: limpieza de código muerto (dos funciones sin llamadas y seis
fuentes huérfanas del laboratorio de movimiento), espacios en blanco, rutas
locales sustituidas por variables de entorno, herramientas de verificación
dentro del repositorio (`tools/native_renderer/pipeline/`), documentación y
un arreglo de la prueba `native_game_preparer`, que fallaba en un clon limpio.

Comprobado (no es evidencia de consola ni de FPS):

- CI de GitHub reproducido en un clon limpio: comprobación del repositorio,
  dependencias públicas, compilación de host con sanitizadores, CTest (tras el
  arreglo), pruebas de herramientas y de audio/guardado.
- Azahar, Bob-omb: 26/26 capturas idénticas a BUILD 133 con Lock FPS 30 y 60
  (perfil Old) y con el perfil New.
- CIA136 instalado, política 80 MB, Bob-omb completo (24 informes, 4 zonas)
  con 30 y con 60: 0 errores, 0 segundas imágenes perdidas, 72/72 capturas
  iguales entre 30 y 60 y frente a CIA135 salvo 9 píxeles del último dígito
  de la etiqueta «BUILD» en la pausa; memoria lineal libre 11,3 MB (30) y
  10,6 MB (60).
- CIA SHA-256: `12db37f62a69e5e5851921145ad16a73ddc06ed14a32008b703e804cab0aa413`.

Pendiente en consola (Old): lo mismo que en BUILD 135.

## BUILD135 — segunda imagen del par más barata y tres bugs del 2D, 2026-11-15

Sustituye a BUILD134, que se entregó por QR pero la consola no llegó a
descargar (el servidor solo registró la descarga de verificación del Mac).
Lleva todo lo de BUILD134 (sección siguiente) más:

- [x] **Segunda imagen del par de 60 en Old**: sale de la misma captura que la
  intermedia (`PoseHistory::midpoint` solo cambia matrices de partes y
  posiciones de skin). Ya no vuelve a validar ni subir quads, no reconstruye
  sombras, no compara el mapa y no dibuja la pantalla que no presenta
  (`submit_overlapped` solo transfiere la del 3D). En Azahar, el costo de CPU
  de la segunda imagen en Bob-omb baja de 2,76 a 2,23 ms (es solo un dato
  relativo de emulador, no de consola). El plan de lotes sigue rehaciéndose:
  agrupa por igualdad de matrices, así que reutilizarlo no sería exacto.
- [x] **Bugs del 2D** (código PPU fijado, corregido por el preparador
  `ppu_bug_fixes_patch`):
  1. la capa OBJ incremental de la ruta CPU no se invalidaba al cambiar la
     paleta OBJ extendida (DISPCNT bit 31): colores de sprite viejos;
  2. BLDCNT modo 1 con el fondo como primer y segundo objetivo: la
     publicación GPU del mapa dibujaba el fondo sin mezclar y la composición
     CPU (la referencia) lo mezcla consigo mismo; ese estado pasa a la CPU;
  3. `shift_rows` movía un puntero con un producto negativo `int × size_t`.
  Ninguno de los dos primeros estados aparece en los 413 volcados de consola
  y emulador (BLDCNT siempre en modo 0, paleta extendida nunca activa).
- [x] Los volcados con Lock FPS 60 ya no mezclan la segunda imagen en sus
  contadores de dibujos y pasadas.
- [x] Desarrollo: `SM64DS_FPS_LOCK_CYCLE=N` (30 → 60 → Unlocked cada N
  actualizaciones) para probar el cambio de Lock FPS en plena partida.
- [x] Los cambios de audio y `game_main` hechos directamente en el árbol están
  reflejados en `tools/prepare_integrated_audio.py` (reproduce `sound_native`
  byte a byte) y `tools/prepare_integrated_profiling.py` (BUILD 133 + sus
  líneas = el `game_main.cpp` actual).

Comprobado (no es evidencia de consola ni de FPS):

- Banco del PPU (`tests/native_renderer/test_ppu_cost_cuts.py`, 3 pruebas OK):
  69.846 publicaciones del motor B y 18.000 preparaciones del A idénticas con
  y sin recortes y frente a BUILD 133 con las mismas correcciones; con las
  correcciones 1 y 3, idéntico a BUILD 133 redibujando todos los sprites;
  sanitizador con desbordamiento de punteros activado. Pruebas de host: todas
  menos `native_background_preparation` (falta `ndspy`, igual que antes);
  preparadores 9 + 8 OK.
- Azahar, perfil Old, Bob-omb y bahía: capturas idénticas píxel a píxel a
  BUILD 133 con 30, 60, Unlocked y `sin-recortes.txt` + `audio-espera.txt`;
  perfil New idéntico a BUILD 133 New. Lock FPS cambiando 30 → 60 → Unlocked
  cada 1,5 s durante todo el nivel: completo, 0 imágenes perdidas o
  rechazadas, 26/26 capturas idénticas.
- Estado de la GPU por dibujo con y sin recortes, mismo binario: volcado de
  zona idéntico (82/82); el del nivel se tomó en otro fotograma en cada
  pasada (otro orden de modelos), así que no es comparable.
- Nota sobre `intervalo_ms` en 60 en Old: mide cuándo termina la CPU cada
  imagen, no cuándo la muestra la LCD; puede salir 7/28 ms con 60 FPS reales
  (ya pasaba en BUILD134). Con 60 imágenes por segundo y cada una retenida al
  menos un refresco por los cercos, cada imagen se ve exactamente uno.
- CIA135 instalado en Azahar Old, política 80 MB, Bob-omb completo (24
  informes, 4 zonas) con el valor por defecto (30) y con `fps_lock = 60`:
  0 errores, 0 segundas imágenes perdidas, 72/72 capturas idénticas entre 30
  y 60 y frente a CIA134 salvo la etiqueta «BUILD 135» de la pausa; memoria
  lineal libre 11,3 MB (30) y 10,6 MB (60). Evidencia:
  `build/native-renderer/playable-r1/hardware/build135/validation.json`.
- CIA SHA-256: `3cf0f412b74f8368c2fdc7b2b97a5c39b8d96492eb7827330f6550d37e969f59`.
- Sin probar: HOME/tapa a mitad de un par (Azahar con applets HLE no lo
  reproduce) y todo el rendimiento real.

Pendiente en consola (Old): recorrido con el valor por defecto (30) y otro con
Lock FPS 60 elegido en el menú antes de SELECT+START; opcionalmente uno con
`sin-recortes.txt` para el A/B.


## BUILD134 — recortes de costo exactos, «Lock FPS» 30/60/Unlocked y par de 60 en Old, 2026-11-14

Petición del propietario: análisis exhaustivo y medido del rendimiento, buscar
bugs y recortar costo sin cambiar la imagen; «Lock FPS» con 30, 60 y Unlocked
(por defecto 30 en Old y 60 en New); que una Old pueda llegar a 60 al quitar el
límite de 30. Las decisiones salen de `dumps/prueba133` (consola real, 1.499
informes); Azahar solo comprueba que arranca, avanza y que la imagen no cambia.

Medidas base (BUILD 133, Old 3DS, por actualización de 33,4 ms):

- hilo del juego 12,2 ms (lógica 5,6; preparación 2D 2,7; productores 3D 3,9);
- hilo de render 11,4 ms por imagen; GPU 8,75 ms por imagen;
- el juego espera al mezclador de audio 1,75 ms por actualización (esperas
  sueltas de hasta 76 ms): el mezclador retiene su candado el 77 % del tiempo
  con la cuota del 30 % del núcleo 1;
- preparación 2D ≈ 1,72 ms fijos + 0,245 µs por píxel de sprite del motor B;
  ~72 µs de CPU por dibujo; subida 1,4 ms fijos.

Cambios (todos exactos salvo donde se dice; `sdmc:/3ds/sm64ds/sin-recortes.txt`
vuelve a las rutas de BUILD 133 para medir A/B en consola):

- [x] **Lock FPS** llega al render nativo (antes no llegaba: bug). Valores
  30 / 60 / Unlocked; Auto = 30 en Old, 60 en New. 30 = una imagen exacta por
  actualización. 60 y Unlocked = imagen intermedia interpolada + imagen exacta.
  Desarrollo: `SM64DS_FPS_LOCK=0|1|2` (Unlocked/30/60) en `env.txt`.
- [x] **Par de 60 en Old** (render y juego comparten el núcleo 0): la imagen
  exacta se construye en un segundo búfer de comandos, con una cola GX que solo
  graba, mientras la GPU dibuja la intermedia; después se añade a la cola del
  SDK. Antes `C3D_FrameBegin` esperaba esos 8,75 ms con el núcleo parado. Ciclo
  previsto ≈ actualización + imagen intermedia + imagen exacta. Entre las dos
  imágenes siempre pasa un refresco de LCD; con límite 60 la intermedia espera
  además a que la exacta anterior esté en pantalla (al final de su CPU, no al
  principio). Guardia de presupuesto: solo se interpola si actualización +
  las dos imágenes caben en el 92 % del periodo; si una actualización
  encuentra el render ocupado con un par, se vuelve a una imagen por
  actualización durante 30 actualizaciones (se duplica hasta 480).
  Costo de memoria lineal: 256 KB (segundo búfer) + segundo búfer de skin.
- [x] **Audio**: el mantenimiento por imagen ya no espera al mezclador (se
  reintenta en imágenes posteriores; bloquea solo tras 16 fallos seguidos) y
  un lote de comandos que encuentra el candado ocupado se entrega después de
  pasar la imagen al render. `sdmc:/3ds/sm64ds/audio-espera.txt` restaura la
  espera. Reflejado en `tools/prepare_integrated_audio.py` (reproduce el árbol
  byte a byte) y `tools/prepare_integrated_profiling.py`.
- [x] **Comandos de GPU** (`pica_state.hpp`, `citro3d_backend.cpp`,
  `native_uniforms.*`): cambio de malla 52 → 4 palabras; culling, profundidad,
  stencil, alfa y mezcla como escrituras de 2–4 palabras en vez del bloque de
  34; constantes del programa una vez por imagen; TEV 2–5 y unidades 1/2 solo
  en la primera pasada. Medido en volcados (cuentas, no tiempo): Bob-omb
  11.154 → 9.034 palabras con los mismos 80 dibujos (−19 %); bahía 11.806 →
  9.442; Bowser oscuro 13.806 → 11.502. Estado de registros idéntico en cada
  dibujo con y sin recortes (`tools/native_renderer/pipeline/compare_gpu_state.py`).
- [x] **CPU del render**: luces empaquetadas en sitio, `BatchSpan` toma
  prestadas matrices y luces (1,1 KB → 368 B por tramo), validaciones
  duplicadas fuera, skin validado mientras se copia.
- [x] **Productores 3D** (`model_instance.cpp`, `scene_producer.cpp`,
  `game_model_pose.cpp`, `game_materials.cpp`): 41–61 % menos instrucciones
  ARM por actualización en escenas del catálogo (Unicorn, compilador de
  consola, seis modos de FPSCR, mismos bits). Skin sin movimiento de huesos:
  −77/−81 %. +88 KB de BSS.
- [x] **2D del motor B y A** (`ppu.cpp` generado, `ppu_cost_cuts.inc`,
  `ppu_affine_*.inc`): comparación del memo solo de los bytes que el mapa
  nombra (74.876 → ≤25.724 bytes); sprites sin cambios no se vuelven a
  rasterizar; barrido por palabras del motor A.
- [x] Bugs corregidos: Lock FPS sin efecto; 2D obsoleto tras un fallo de
  sellado (`a_cached/b_cached`); subida de skin sin medir (la «subida» de los
  informes sube por eso); sombra de planos comparada contra una subida no
  completada (`upload_plane`); caché de luces no invalidada al rechazar un
  programa; medias de productores divididas por actualizaciones sin captura.
- [ ] Bugs encontrados y no tocados (código PPU fijado): capa OBJ de la ruta
  CPU no se invalida con la paleta OBJ extendida; con BLDCNT modo 1 y fondo
  como ambos objetivos la ruta CPU mezcla el fondo consigo mismo; `shift_rows`
  resta un entero negativo por `size_t` (UBSan).

Informes nuevos: cabecera `limite 30|60|sin FPS`; bloque «interpolacion»
(intermedias, exactas en paralelo con la GPU, presupuesto cabe/no cabe,
segunda imagen perdida, costo de cada imagen, actualizaciones en espera);
`juego_espera_tras_imagen_ms` y `lotes_aplazados` en audio.

Comprobado (no es evidencia de rendimiento ni de consola):

- Pruebas de host: todas pasan menos `native_background_preparation` (falta
  el módulo `ndspy`, igual que antes). Nuevas: `native_pica_state` (381.051
  dibujos con registros idénticos), `native_uniform_residency`,
  `native_render_cuts`, `native_producer_cuts` (42/42 mutantes detectados),
  `tests/native_renderer/test_ppu_cost_cuts.py` (69.846 publicaciones del
  motor B y 18.000 preparaciones del A idénticas; no está en CMake).
- Azahar, perfil Old, Bob-omb, bahía y Bowser oscuro: capturas de pantalla
  idénticas píxel a píxel a BUILD 133 con límite 30, 60, Unlocked y con
  `sin-recortes.txt` + `audio-espera.txt`. Perfil New (60 por defecto)
  idéntico a BUILD 133 New. En 60/Unlocked Old: 124 de 124 imágenes exactas
  en paralelo, 0 perdidas.
- CIA134 instalado en Azahar Old, política 80 MB, Bob-omb completo (24
  informes, 4 zonas, 5 volcados) con el valor por defecto (30) y con
  `fps_lock = 60` en `settings.ini`: 72 de 72 capturas idénticas entre ambos,
  0 imágenes exactas perdidas, memoria lineal libre 11,3 MB (30) y 10,6 MB
  (60). Evidencia: `build/native-renderer/playable-r1/hardware/build134/validation.json`.
- CIA SHA-256: `0f4581c68a464377396fe72a2c81d22b82e23cf5edc260f0c5f2258505133f10`.
- No comprobado: HOME/tapa a mitad de un par, cambio de Lock FPS en caliente
  desde el menú, y todo el rendimiento real.

Pendiente en consola (Old): recorrido con el valor por defecto (30) y otro
con Lock FPS 60; opcionalmente uno con `sin-recortes.txt` para el A/B. El
bloque «interpolacion» dirá cuántas actualizaciones caben a 60 y el costo
real de cada imagen. Ningún número de FPS de consola se afirma aquí.

## BUILD133 — bloqueo del primer volcado en Old 3DS, 2026-11-14

- Evidencia del propietario: `dumps/prueba131`; primer arranque se detiene
  antes de guardar el bloque PICA interno, segundo en VRAM (4.325.376 bytes).
  Ambos habían generado el informe quieto a ~30 FPS.
- Se retiran las lecturas físicas GPU/VRAM del modo automático. El volcado
  de RAM también excluye dispositivos; comandos, escena y memoria DS siguen.
- Se registra el inicio de cada archivo para atribuir posteriores bloqueos.
- BUILD132 descartada antes de entrega: el CIA puede mapear VRAM como memoria
  estática (confirmado: estado 3, 6.291.456 bytes en memoria.vm de CIA132);
  BUILD133 excluye también los rangos físicos por dirección.
- Validado: CIA133 instalado en Azahar Old, política 80 MB, jardines y planta
  baja completos: 72 informes y 10 volcados sin fallos; una reentrada normal.
  RAM completa con rangos VRAM/DSP sin payload. Pruebas host del guion y
  transferencias pasan. Evidencia: hardware/build133/freeze-fix-validation.json.
- CIA SHA-256: `5e6b5250d972a24ba90ed697a8054bb4bd684cca517b6f72966c83b0cee5c524`.
- Verificación de consola pendiente; no es una mejora de FPS.


## In progress: BUILD131 — test tour beyond the entrances, deep dumps, exact producer savings, 2026-11-13

Owner request: stop testing the same zones, test every level and situation,
dump RAM, VRAM, GPU, graphics commands, errors, performance and game state to
the SD, keep cutting CPU toward a stable frame rate on the Old 3DS, and
deliver a CIA that can be verified on hardware. No number is reserved yet:
the code builds as local experiments (`game-p130-t3`); BUILD131 is built once
the emulator checks pass.

- [x] BUILD128 hardware tour read again per stage (903 reports): game update
  18.7 ms (logic 5.9, CPU 2D 5.0, model producers 8.1 of which skin 2.1),
  render worker 22.9 ms (plane uploads 5.2, geometry 4.5, plan 1.5, end 5.3 on
  that build's core-1 layout). Every level reports thousands of model draws
  with no identity (`ultimo_modelo_faltante=4294967295`): about a third of
  the model calls draw nothing. The screenshots show actors without their
  drop shadow; `ShadowModel`/`CommonModel` take pooled `ModelComponents` whose
  load the hooks never see. To be confirmed by the new census.
- [x] Tour script (`test_mode_script.hpp`): 24 entrance situations (adds
  crouch, crawl, slide kick, air kick), then up to four zones per level with
  three reports each (still, camera turn, running in a circle). Zones are
  live actors of the player's area standing on ground (the game's own
  `dBgCh_Gnd` probe), at least 1200 units from the entrance and from each
  other (farthest-point sampling), entered 220 units beside the actor. A
  death at a zone abandons that zone only and never fails the level; up to 5
  re-entries. State V2 gains `informes_zona`. Host test: 36 reports with four
  zones, two zones, none, death at a zone, last re-entry, resume at a zone,
  development limit.
- [x] Reports are flat files (`LL-level/SS-step.resumen.txt`, `.cuadros.csv`,
  `.arriba.bmp`, `.abajo.bmp`): the BUILD128 tour spent 3.3 s of SD time per
  report, about 2.4 s of it creating the per-situation folder.
- [x] Each report adds the player state (position, speeds, health, water,
  floor, state flags), the model producers by stage (materials, poses,
  normal bases, lighting bases, parts, skin; ms per update), vector
  normalisations per update and the missing-model census: one row per model
  the native route did not draw (calls, reason, file id, bones, materials,
  textures, first material and texture names), also as `.faltan.csv`.
- [x] Deep dump after the first report of a level (`volcado/`) and of each
  zone (`volcado-zonaN/`): one frame's PICA command list with a record per
  draw (`comandos_gpu.bin`, `dibujos.csv`), the semantic scene
  (`escena.json`), textures and render targets, renderer counters and GPU
  memory, GSP register blocks (PDC, transfer engine and the PICA internal
  block 0x401000-0x401BFF, guarded by a marker file so a hang there is never
  retried), every live actor; per level also VRAM (6 MiB, through a staging
  buffer) and the DS-side video memory; the whole process memory
  (`memoria.vm`, SM64VM1) in the first level, in spot runs, or in every level
  when `prueba/ram.txt` exists.
- [x] Tools: `tools/test_mode_dump.py` (command list totals, words and
  uniform words per draw, writes per register group, draw records, registers,
  DS video state, actors), `tools/test_mode_report.py` (both layouts, zones,
  census table across levels).
- [x] Exact producer savings, no output change:
  - vector normalisation (`normalize_columns`, `normalize_xy`: every lit or
    environment-mapped bone with the normalise flag, every billboard) used a
    32-step 64-bit restoring square root and libgcc's shift-subtract 64-bit
    divide; it now uses `ds_math_unit.h` (VFP double divide and square root
    with integer correction), as the game's own math does since BUILD114.
    `tools/test_native_normalize_arm.py` (Unicorn, ARM11 MPCore): 15,572 edge
    and random bases and 3,200 scaled rotations identical; 2,551 -> 1,013
    instructions per basis on rotations (2.52x). Host: 208,000 more bases
    against the independent reference of `normal_basis.cpp`;
  - lighting palette hash: one multiply per word instead of byte-wise FNV
    (72 steps per lit part); ids are assigned in insertion order, unchanged.
- [x] Host: ASan/UBSan ctest 103/104 (`native_background_preparation` fails
  as before without ndspy in CTest's python). ARM11: `game-p130-t3` compiles
  and links (54.2 MB ELF).
- [x] Azahar Old, .3dsx, first run of the new tour (Bob-omb, 2 entrance
  situations + 4 zones, 14 reports, 5 dumps, 29.7 FPS in the emulator): the
  build boots and frames advance; zones were chosen from 19 candidates
  (181 actors, 30 on ground) 8,500-15,000 units from the entrance. What that
  one level found:
  - **Top-screen 2D planes upside down since BUILD114 (fixed).** The owner
    sent a picture of a "stack of discs" under Mario: the health meter. With
    the plane dump (`planos_2d.bin`) the CPU plane was right (meter at the
    top) and the screen was not; decoding the render targets from `vram.bin`
    showed both engine-A surfaces mirrored. `expand_bridge_planes` copied the
    map's sharp step, but the map reaches its sharp surface in two passes
    (each mirrors the rows) and a plane in one. On the Old 3DS (and Azahar
    Old) the whole top HUD came out mirrored at the bottom of the screen: coin
    counter bottom right, meter over the character; the BUILD128 hardware
    pictures show the course intro text upside down too. New 3DS with AA kept
    the front plane on the CPU path and was not affected there.
  - **Stale front plane (fixed).** The same pass never cleared its target and
    the alpha test drops transparent texels, so everything the HUD had drawn
    stayed (the meter's nine slide positions were the "discs"; on hardware the
    course title stayed over the game). The front surface is now cleared on
    every expansion.
  - **Command capture kept the last list only (fixed).** Each cleared pass
    splits libctru's buffer; the capture now runs from the base seen at
    `begin()`: 10,910 words and 78 of 78 draws (was 366 words, 2 draws).
    Per draw: 140 words, 59 of them float uniforms.
  - **The missing third of the model calls is `mat_shadow`** (1 bone, 1
    material, no texture, not a ROM file): `ShadowModel`'s built-in cylinder,
    drawn twice per shadow.
- [x] Shadow volumes. `ShadowModel::RenderAll` draws each volume twice with
  DS polygon mode 3 (mask with polygon ID 0 over back faces, then render over
  front faces). The hooks send the two built-in models (`data_020ad560`
  cylinder, `data_020ad524` cuboid; octagonal/square prisms of half-width 1/16
  from y=-1/8 to 0, decoded from the overlay's display lists) to
  `SceneProducer::capture_shadow`: one `ShadowVolume` (clip matrix, opacity,
  shape) per render call, up to 64 per image. The renderer draws them after
  every occurrence with the target's existing stencil (D24S8): all masks
  (back faces, depth-fail writes the volume's own reference, no colour or
  depth), then all front faces where the stencil holds that reference,
  blended black at opacity/31. APPROXIMATION, labeled in the code: the DS
  also keeps a volume off polygons sharing its polygon ID (the actor's own
  feet); polygon IDs are not carried. Azahar Old, Bob-omb: shadows under the
  player, Goombas, Bob-ombs and trees; `modelos_faltantes=0` (was 1,554 in the
  first report alone). Host test for the frame storage.
- [x] Shadows in two draws per image. Every volume shares the projection, so
  `build_shadow_quads` (`shadow_volumes.hpp`) places their vertices in view
  space on the CPU (64 per cylinder, 32 per cuboid) into one dynamic quad
  stream: one mask draw and one darkening draw for all of them, with the
  opacity in the vertex alpha. Overlapping volumes darken a pixel once (the
  darkening pass releases the stencil), as volumes of one polygon ID do on
  the DS. Host test: closed prisms wound outwards, posed, capacity. Azahar:
  same picture as two draws per volume; a zone with seven shadows went from
  92 to 79 draws. `sdmc:/3ds/sm64ds/sin-sombras.txt` leaves shadows out, to
  measure their cost on a console.
- [x] DS fog, opt-in experiment (`sdmc:/3ds/sm64ds/niebla.txt`, or
  `SM64DS_NATIVE_FOG=1`): the game sets fog every image (`Stage::RenderFog`:
  DISP3DCNT, FOG_COLOR, FOG_OFFSET, FOG_TABLE; a second set under water) and
  the native route ignored it. `ds_fog.hpp` models the DS density (depth
  minus offset, shift, 32-entry table with interpolation; with SWAP_BUFFERS=3
  the depth is the clip-space w in 20.12) and fits a PICA fog table to it
  (128 intervals of window depth, least squares per interval); materials
  carry POLYGON_ATTR bit 15 and the backend switches GPU_FOG per material.
  Bob-omb's registers from the dump: enabled, shift 6, grey (20,20,20),
  linear ramp to 45% at w=64; parts in view reach w=56. Azahar: fog on greys
  the middle and far ground by 28-30% and leaves the near stone untouched.
  APPROXIMATION, which is why it is off by default: the PICA indexes its
  table by window depth (0.0625/w here), so the whole ramp falls in the last
  three intervals; largest error 15.5 of 128 (near w=16-24 it fogs too much).
  Host test with those registers. Not compared with a DS picture.
  Exact route, researched and not built: PICA W-buffering
  (GPUREG_DEPTHMAP_ENABLE=0, depth = clip z * scale + w * offset). The native
  clip z is the constant -1/16, so scale=-16 and offset=-1/F give
  depth = 1 - w/F, linear like the DS depth buffer; with F=128 each table
  interval is one unit of w and the DS table is reproduced exactly. It
  changes the depth of every draw, so it needs a console before it ships.
- [x] Dumps: every zone now adds the three 256x192 planes the 2D bridge
  composed (`planos_2d.bin`) and the sprite side of the DS video memory (I/O,
  palettes, OAM, OBJ VRAM; the level dump keeps all of it).
  `tools/test_mode_dump.py --sprites` lists every on-screen OAM entry,
  `--planos DIR` writes the planes as PNG; `tools/test_mode_sheet.py` makes
  one contact sheet per level from the report pictures (no imaging library).
- [x] Runner: 3 s of camera settle after a zone teleport (the first run took
  the zone's first picture before the camera arrived); a zone after which the
  game never returns control costs the zone, not the level (castle basement
  and upper floor ended INCOMPLETO in the first long run); reports name the
  capture stage that refused a model; the floor under the player is probed
  from above the feet. Host tests updated.
- [ ] BUILD131 number, CIA (80 MB mode) check, LAN delivery.
- [ ] Hardware: tour with the new reports. No console gain is claimed from
  instruction counts.

Not done, with the measured reason:
- 60 FPS on the Old 3DS: the BUILD128 console data is 18.7 ms of game thread
  plus 15-23 ms of render worker per image on one core; 30 FPS is not yet
  held in 22 of 50 levels.
- Skin on the GPU (seam triangles are transformed on the CPU: 2.1 ms average,
  5.7 ms in Jolly Roger Bay and the Bowser fights, plus their upload): the
  batch shader's 8-matrix palette can take them with static geometry and a
  per-vertex slot; it needs per-bone matrices in the captured frame and
  emulator checks.
- Shadows: need the pooled models' identity and a stencil pass.

## Experimental BUILD130-armv6 — ARMv6 mixer + mixer time in reports, 2026-11-09

Local experiment only; the official build number stays 130 (only the
generated header carries the tag). Private branch `perf/armv6-2026-11-09`.
Full report: `docs/PERF_ARMV6_MIXER_2026-11-09.md`.

- [x] Diagnosis with file:line evidence (audio, cores, RSF, flags, divisions,
  GPU, caches, I/O, telemetry). BUILD128 console log: `audioLoad=27.8%`.
- [x] Unicorn profile of the devkitARM mixer on the game's sequences: 85% in
  the per-voice loop (`Player.cpp:533-547`, ~45 instructions per voice sample).
- [x] `audio_mixer_kernels.hpp` + `prepare_integrated_mixer.py`: SMLAWB voice
  loop (asm, 14 instructions per voice sample), SSAT/PKHBT output, segment-only
  clears, native-rate clock without 64-bit work. Whole mixer 599 -> 228
  instructions per sample (2.63x), bit-identical.
- [x] `tools/test_integrated_mixer_armv6.py`: pinned vs ARMv6 vs portable in
  Unicorn (ARM11 MPCore), 6300 perturbed render calls + 8000 kernel cases.
- [x] Report section `== audio ==` (mix/pump ms per s, load, worst 64-sample
  block, game-thread waits, queue drains) and `audio=X%` per situation.
- [x] Host: CTest 103/104 (ndspy env failure as before); audio suite incl.
  new TSan `mixer-lock`; build-tools tests.
- [x] Azahar smoke (one instance): .3dsx armv6 and portable switch, CIA 20/20.
- [ ] Console: tour with BUILD130-armv6, then the same with
  `sdmc:/3ds/sm64ds/mezclador-portable.txt`; Old and New.

## Candidate BUILD130 — BUILD128 hardware tour analysis, 2026-11-08

BUILD129 had the same changes but was superseded before delivery by the
review's lid fix below. Its number is not reused.

Owner request: analyse the third tour, chart it, and improve everything
toward stable 30 FPS; not just a revert of what got worse.

- [x] BUILD128 hardware verdict: render on core 1 (80%) plus audio on core 0
  is a net loss. GSP and FS share core 1 with the worker (GPU 9 -> 21 ms,
  finish 0.25 -> 5.3 ms, SD loads 3x slower) and the mixer costs the game
  thread +40%. BUILD129 restores the BUILD123 layout (render on core 0 below
  the game thread, audio on core 1 at 30%); the core-1 layout stays opt-in
  (`sdmc:/3ds/sm64ds/nucleo-render-1.txt` or env
  `SM64DS_FRESH_NATIVE_WORKER_CORE=1`). MaxCpu stays 0xD0.
- [x] Stalls: 92 of the 95 game-thread stalls of 100-314 ms were the PTMU
  shell poll every 15 updates. The lid now runs on a priority-0x3e thread
  (500 ms), and the backlight calls take a LightLock. The first poll of each
  boot applies the lid state unconditionally (BUILD130, from review):
  setup_power's guard lights the screens again before the run starts, so a
  level relaunched with the lid closed otherwise stayed lit.
- [x] Normal play: the once-a-minute game-thread `fflush` froze play for
  0.4-0.7 s. A priority-0x3f thread now flushes every 20 s. A printf
  issued during that write still waits for the FILE lock, but the write is
  about 10 KB instead of 25 KB.
- [x] Cadence: the fixed timeline repaid each 3.9 s load at ~1.85x speed for
  seconds (53% of 01-quieto intervals advanced 3+ updates). A frame more
  than 6 display frames late now postpones the timeline and keeps only
  2 frames of debt. `Native cadence stalls: forgiven=N forgivenMs=X`;
  `PLAYER_CADENCE_KEEP_STALLS=1` restores the old behaviour.
- [x] Exact render savings (bit-identical output):
  - map foreground upload compares only the 8-row bands whose OBJ boxes
    changed (`ppu_native_affine_bands`, ~2.1 ms);
  - an unchanged map is not redrawn;
  - the pass target cache replaces a texture-table walk per pass;
  - a rotating or scrolling bottom map whose only change is its 16 affine
    register bytes rebuilds just the descriptor's affine fields instead of
    converting palettes and re-snapshotting ~100 KB (~0.65 ms average,
    1.5 ms while turning). `PPU_NO_AFFINE_FAST=1` disables it;
    `PPU_AFFINE_FAST_CHECK=1` runs both paths and counts mismatches.
- [x] Harness:
  - H4: an exit noticed while settling home for an unstarted step resumes
    at that step (BUILD128 lost "quieto-final" in 7 levels). Steps without
    a home return still count as started at once, so a fall caused by the
    previous step that is noticed early in such a step skips that step;
  - H6: the first START tap waits 3 s of lock and happens only before the
    script runs, so entry locks are never tapped;
  - H7: after the run ends, the game parks (muted, 50 ms per update, 250 ms
    with the lid closed) until START. `PRUEBA fin: juego en reposo hasta
    START`.
- [x] Host: ASan ctest 102/104. `native_background_preparation` fails as
  before because CTest's system python has no ndspy; the
  `native_game_preparer` test now passes against vendor sources. CIA policy
  OK.
- [x] Independent review: no high-severity issue; affine fast path and
  cadence match the slow path (the Clock was run on the host).
- [x] Azahar (one instance, BUILD130):
  - .3dsx Bob-omb 4/4 at 29.8 FPS with `PPU_AFFINE_FAST_CHECK=1`: checks=129
    mismatches=0, `watcher=1`, one load stall forgiven (385 ms), rotated GPU
    map and top screen correct in the report images;
  - last tour level to the final panel: LED green, `PRUEBA fin: juego en
    reposo hasta START`;
  - CIA (53,076,928 bytes, SHA-256 cfe97e52...903d, MaxCpu 0xd0): castle 51
    20/20 at 29.9 FPS (pause p95 58.2 ms: the CPU 2D recompose is still the
    largest event cost).
- [x] Delivery: `hardware/build130/lan-delivery` (QR and download verified);
  BUILD128 server stopped. The SD card was not mounted.
- [ ] Hardware: fourth tour; compare per level with BUILD123/128.

## Candidate BUILD128 — unattended test runs with the lid closed, 2026-11-08

Owner request: after SELECT+START the console must not sleep when closed,
the run continues, and the notification LED turns green when it finishes.

- [x] libctru (`source/services/apt.c`): aptInit asks APT to sleep if the
  shell is closed right after the first wakeup, with sleep allowed by
  default, so every relaunched tour level would sleep before main. The game
  overrides `__appInit` (test_mode.cpp, libctru's sequence) to refuse sleep
  immediately after `aptInit` (no blocking call in between; the query is
  answered by libctru's lower-priority event thread on core 0) and allows it
  again after the SD mount unless `prueba/estado.txt` exists.
- [x] `rom_setup_3ds` ends every boot in `setup_power::Scope::completed()`,
  which waits for an open lid with a blinking green LED; a pending test run
  now skips that wait and blink (`sm64ds_test_mode_pending`, weak).
- [x] Test runs own power: sleep refused for the whole run, backlights off
  while the lid is closed (PTMU shell state every 15 updates), notification
  LED solid green when a tour or spot run finishes, red when the tour cannot
  continue (state not writable, NO ARRANCO), off when cancelled and when
  START leaves the final panel. The DS game never sees the lid (nothing sets
  the 0x027fffa8 fold bit), so its PM sleep path stays unused.
- [x] Host: setup power guard test, test-mode script test. Game ELF:
  `__appInit` (strong) calls srvInit, aptInit, aptSetSleepAllowed, hidInit,
  fsInit, archiveMountSdmc, stat; rom_setup calls `sm64ds_test_mode_pending`.
- [x] BUILD127 Azahar: last tour level to the final panel (LED line missing:
  the result was not flushed before the session ended). BUILD128 logs and
  flushes: .3dsx and CIA (53,072,832 bytes, SHA-256 9f60993e...6c86, MaxCpu
  0xd0) both log `PRUEBA power: sleep refused=1 shell=1 lcd=1` and
  `PRUEBA notification LED green result=00000000` after RECORRIDO FINALIZADO;
  CIA castle 51 20/20 at 29.7 FPS, render core 1, cpuTime 80/80. Azahar has
  no lid: closed-lid behaviour and the physical LED need the console.
- [x] Delivery: `hardware/build128/lan-delivery` (QR and download verified);
  BUILD126 server stopped.
- [ ] Hardware: tour with the lid closed; LED green at the end.

## Candidate BUILD126 — BUILD123 hardware tour analysis, 2026-11-08

Owner input: BUILD123 tour on an Old 3DS, `prueba/01` (50 levels, 754
situations), `native-test.log` (final boot) and `native-test-anterior.log`
(level 51), `native-test.sav`. Analysis scripts lived outside the repository.

- [x] Improved: all 50 levels ran, 0 hangs (BUILD120 stopped at boot 26);
  threads 7/32 at every report (no leak); package verification 2.1-2.4 s
  (28 s before); level loads 0.9-1.9 s. The 24 levels both tours measured:
  22.6 → 27.7 FPS mean, all 24 faster (Tiny-Huge big 15.2 → 28.8 with the
  GPU map retry, Lethal Lava 15.5 → 26.5, Wet-Dry 22.5 → 29.5). 28 of 49
  measured levels at 29+ FPS.
- [x] Still slow: Jolly Roger Bay 17.5, Bowser fights 15.4/20.1/17.2, Whomp
  22.1, secret 42 24.8 FPS. In each, game update (17-20 ms) + render CPU
  (15-19 ms) exceed 33 ms on the shared core 0; `secas_por_render_ocupado`
  26-57 per situation. Pause: open ~450 ms (three images of 2D CPU
  preparation + composition + 1.1-1.5 MB uploads), then an 83 ms image every
  8 frames (top screen re-prepared 10 ms + composed 35 ms).
- [x] Core 1: PM grants the application `MaxCpu & 0x7F` percent of core 1
  (Luma3DS `sysmodules/pm/source/reslimit.c`; bit 7 = multi-thread
  preemption mode). The exheader's 0x9E capped it at 30, which is why APT
  refused 80 with d8e05bf4 in BUILD88. BUILD124: MaxCpu 0xD0 (80, same
  mode); on the Old 3DS the native renderer asks for 80 and, when granted,
  the render worker takes core 1's single application thread while the
  mixer pumps on core 0 above the game thread. Refusal (Homebrew Launcher,
  older CIA) or `sdmc:/3ds/sm64ds/nucleo-render-0.txt` keeps the BUILD123
  layout. CIA verifier checks descriptor 0 = 0xD0. Reports: `nucleo_render`,
  `nucleo_audio`, `render_comparte_nucleo`; `render_cpu_estimado` no longer
  subtracts game updates from a worker on its own core.
- [x] Courtyard: its entrance opens the pause screen's save submenu (Save &
  Continue / Save & Quit / Continue, PS states 0xc/0xd), also in Azahar with
  a new save, and holds the player pause lock; 13 situations measured that
  menu (15 FPS, 35 ms CPU composition) until the pause step's START closed
  it. BUILD125: the runner taps START every 2 s while the lock lasts with
  control (pause disabled: START cannot open a pause); BUILD126 tags the
  level only when a lock ends right after a tap and closes any pause that
  opens outside the script's pause step (a tap landing as an entry lock
  ends).
- [x] Tour save: the BUILD123 tour played on the owner's `native-test.sav`
  and the script's A presses could select Save & Continue. BUILD124: runs use
  `prueba/partida.sav` (removed per tour level, copied from the owner's save
  for a spot run); the owner's save is never read or written.
- [x] Deaths: 20 of 50 levels ended "entered another level" (lava, falls into
  the void, checked on the last screenshots: real deaths, not warps).
  BUILD124: a level that already ran its script re-enters from a fresh boot
  after the fatal step, keeping its reports and averages (max 3 re-entries;
  state V2 keys `paso`, `informes`, `reentradas`, ...).
- [x] 2D bridge: the per-state foreground table also covers windows and
  per-OBJ alpha (window bits 0/5, own-alpha ≤ 16), exact by construction and
  by host check (60 M windowed combinations); the inlined lookup replaces the
  12-field per-pixel policy in menus.
- [x] Host: 103/104 CTest with ASan/UBSan (the background-preparation test
  needs the project Python with ndspy: 10/10 there), preparer 9/9 (the
  pinned vendor and render60 sources both reproduce the generated
  `player_present.cpp` byte for byte), CIA policy 5/5, test-mode script
  (tour/spot/death re-entry/courtyard menu/stray pause), bridge table.
- [x] BUILD124 Azahar courtyard: `Fresh native audio: core=0 ... renderCore1=1`,
  `render worker: core=1 ... sharesGameCore=0`, cpuTime 80, save
  `prueba/partida.sav`; the menu still stayed open until the pause step.
  BUILD125 Azahar: courtyard 19/20 at 29.8 FPS (menu closed by START, two
  re-entries after running through the castle door), basement 20/20 29.9,
  castle 2F 20/20 29.7.
- [x] BUILD126 Azahar .3dsx courtyard: 19/20, 29.7 FPS, "menu cerrado con
  START", re-entries 1-2 continue at "correr-curva"/"salto-largo". CIA126
  (53,072,832 bytes, SHA-256 1ba95150...d4d692; verifier: MaxCpu 0xd0, 80%)
  installed in Azahar: boot `cpuTime=0/80` (the exheader cap), then 80/80,
  audio core 0, render worker core 1, save `prueba/partida.sav`; Jolly Roger
  Bay 20/20, 29.7 FPS; Jolly Roger ship re-entered after "camara-l". The tag
  can also mark a level whose entry lock (course title card) ends right
  after a tap: harmless. Azahar does not model core-1 time slicing: the
  speed-up needs the console.
- [x] Delivery: `hardware/build126/lan-delivery` (QR decoded to the exact URL,
  LAN download SHA-256 equal); BUILD123 server stopped. Physical installation
  not verified.
- [ ] Hardware: owner tour with BUILD126 (check `sistema`: nucleo_render=1,
  cpu_nucleo1=80; if the console refuses 80 the log says "80% of core 1
  refused" and the BUILD123 layout runs).

## Candidate BUILD123 — BUILD120 hardware tour analysis, 2026-11-08

Owner input: BUILD120 tour on an Old 3DS, `prueba/01` (25 levels, 434
situations, 414 CSVs), `native-test.log` (boot 26) and
`native-test-anterior.log` (boot 25). Analysis scripts lived outside the
repository; numbers below come from deduplicated `cuadros.csv` rows.

- [x] Result: 22.6 FPS mean over 24 levels (Whomp 18.7, Jolly Roger Bay 13.6,
  Lethal Lava 15.5, Tiny-Huge big 15.2; castle levels 26-28). 18 complete,
  7 incomplete ("entered another level"), no crash before boot 25.
- [x] Report artifacts: every 1.4-2.5 s render job was the last row before a
  report. `write_report` created the situation folder (~2.4 s per new folder
  on the owner's SD) before draining the worker, while the worker was
  blocked on its own log line: game_main runs the game with stdout
  unbuffered. The mkdir time also escaped the cadence pause, so the game
  caught up afterwards. BUILD122: buffered log (32 KiB) from the first scene
  change; folders, files and the result line inside the drained pause.
- [x] Shared core: Old 3DS game thread and render worker both run on core 0,
  the worker one priority lower, so worker wall times include game updates.
  37.4% of 55,585 updates captured a scene and discarded it (worker busy),
  23.5% were dry (late), 38.7% produced images. BUILD122: an update that
  starts while a shared-core worker is still drawing is dry
  (`secas_por_render_ocupado`; `SM64DS_FRESH_NATIVE_BUSY_DRY=0` disables).
- [x] 2D CPU path: 54 of 414 situations at 13.1 FPS median (25.6 elsewhere).
  Tiny-Huge big: moving situations 25 ms engine B preparation + 11 ms
  composition + 786 KB (2x CPU plane) per image; Azahar keeps the GPU map
  there (1.8 ms, ~120 KB). A single failed GPU map publication set a sticky
  flag for the boot. BUILD123: the 1 MiB map raster upload evicts like a bank
  load on budget/capacity/memory errors; a failure is retried after ~10 s,
  three times per scene, and a new scene resets it.
- [x] Boot: `verify-cache` read 2450 members through the small stdio buffer,
  28 s per boot (0.55 MB/s). BUILD122: unbuffered 512 KiB reads across
  members and a slicing-by-4 CRC (host check: 4,400 vectors equal to the
  bytewise CRC; corrupt bytes at start/middle/end still rejected). Azahar:
  3.3 s.
- [x] Tour stop: boot 25 could not create the render worker, boot 26 not the
  game thread (heap intact). Every boot had failed to create the fs prefetch
  thread on core 1 (libctru: one application thread there, owned by audio).
  BUILD122: no prefetch thread (`SM64DS_NO_FS_PREFETCH=1` unless env.txt
  says otherwise), `Resource limits` lines at boot and per worker, a
  `sistema` section per report, and `NO ARRANCO` keeps the level for the
  next boot and returns to HOME instead of relaunching into the failure.
- [x] Profiling: CSV `game_overlap_ticks`, nine `stage_*` columns and
  `route_2d`; summaries add `render_cpu_estimado`, `etapas_dibujo`,
  `ruta_2d`. BUILD123 logs `PPU A prepare: changed=...` (fine-profile/test
  runs): which top-screen input changed before a full 2D preparation.
- [x] BUILD121 failed to compile (number reserved). BUILD122 and BUILD123 build;
  host tests pass (frame timeline, test script, preparer, packed assets with
  ASan/UBSan).
- [x] Azahar: BUILD122 .3dsx level 1 (4 situations, 29.7 FPS); BUILD122 CIA
  (80 MB) Tiny-Huge big 20/20, 29.9 FPS; BUILD123 .3dsx Tiny-Huge big 8/8
  with the GPU map throughout; BUILD123 CIA castle 1F 20/20, 29.9 FPS, pause
  included, 6.1 MiB libc heap free after it; courtyard 20/20, 30.3 FPS.
  Delivery: `hardware/build123/lan-delivery` (QR decoded, download SHA-256
  equal); physical installation not verified.
- [ ] Hardware: next owner tour with BUILD123.

## Candidate BUILD120 — hardware crash in the castle pause, 2026-11-07

Owner report: the BUILD114 tour ran on an Old 3DS (level 1 complete 20/20,
level 2 up to report 13) and crashed at the pause step of the castle 1F.
Inputs: Luma `crash_dump_00000007.dmp`, `native-test.log`, `prueba/01`.

- [x] Diagnosis. Luma: data abort on core 1 in `memcpy` called by
  `sm64ds_mixer_render` from `Sm64dsSoundOutput::pump` (audio thread), write
  to 0x141c9e00 (DFSR 0x805, section translation fault), source 0x08e5a7e0
  still mapped. The exheader's kernel 2.33 puts the linear heap at the old
  0x14000000 mapping, so this is the NDSP PCM buffer itself (1.8 MiB into the
  20 MiB linear heap), not a corrupted pointer. The process was exiting:
  `abort()` -> `_exit` -> libctru `__appExit` -> `__libctru_exit`, which
  unmaps the linear heap first and the app heap after, while nothing had
  stopped the core-1 audio thread. devkitARM's `std::terminate` prints
  nothing. The last log lines are the second PPU cache way allocated for both
  engines (2 x 2,559,232 bytes) as the pause opened; Azahar allocated it for
  one engine only and passed. Most likely cause: libc heap exhaustion ->
  uncaught `std::bad_alloc` (HostAllocator) -> abort.
- [x] Exit safety: `userAppExit` (called by libctru before either heap is
  freed, on every exit path) stops the audio thread (bounded join), closes
  NDSP and waits for the render worker; an abnormal exit returns to HOME.
- [x] Fatal diagnostics: `std::terminate` and SIGABRT (per thread: main,
  render worker, audio) log the reason (`std::bad_alloc` named), libc heap
  (used, free chunks, never-claimed sbrk range), linear free, the unwinder's
  backtrace and code addresses on the failing stack, then flush.
- [x] Memory: the second PPU cache way is refused unless the libc heap keeps
  6 MiB free after it (`SM64DS_PPU_WAY_RESERVE_KB`); a refusal only
  re-prepares 2D planes on alternating display configurations. The worker's
  `operator new` fallback into the unlocked game heap is reported.
- [x] Test tour: each report and `resumen.txt` carry the libc heap free; a
  boot that dies keeps its log as `*-anterior` and the next boot copies it to
  `LL-nivel/registro_cuelgue.txt`; after an abort/terminate during the tour
  the game relaunches itself (HOME chainloader) and continues.
- [x] Confirmed in Azahar: the .3dsx (new linear mapping) logs
  `samples=0x301c9e00`; the CIA's old mapping puts the same buffer at
  0x141c9e00, the exact Luma fault address.
- [x] Heap budget. The Azahar .3dsx runs in 96 MB mode; the hardware CIA in
  80 MB: code 24.5 MiB + stack 1 MiB + linear 20 MiB left ~34.5 MiB of libc
  heap. The castle 1F uses 28.5 MiB before and 34.4 MiB after the pause
  (one engine's second cache way); both engines on hardware exceed it.
  BUILD118 moves 4 MiB from the linear heap (16 MiB; fresh native peak 7.0
  MiB on hardware in the title menus, 6.2 MiB in the 50-level Azahar tour) to
  the libc heap, on top of the cache-way reserve.
- [x] BUILD115 failed to compile (number reserved, never reused); BUILD116
  built without the development fault switch; BUILD117 adds
  `SM64DS_DEV_FAULT=terminate:N|abort:N` (env.txt, .3dsx only); BUILD118
  adds the 16 MiB linear heap; BUILD119 hands a fatal error on the render
  worker or audio thread to the game thread (the failing thread parks; if
  the game thread is blocked for 3 s, it exits itself) and adds
  `SM64DS_DEV_FAULT=worker:N`.
  BUILD120 logs startup unbuffered (game_entry kept a 32 KiB buffer that
  smoke_main replaced by an unbuffered log anyway), so a boot that stops in
  setup leaves its last line.
- [x] Azahar Old, .3dsx BUILD119/120 failure checks: `terminate:2120` after
  the pause (second cache way forced off: pause 30.7 FPS, p95 56.8 ms, same as
  with it) logs `FATAL: std::terminate hilo=juego: std::bad_alloc`, heap,
  addresses (addr2line: `__cxa_throw`, the update hook, `func_0204fafc`),
  stops audio/NDSP/render and asks the tour to relaunch; `worker:400` logs
  `hilo=render`, parks the worker and the game thread exits.
- [x] verify114.py carries the boot's log to the next boot (as on the SD);
  it reads the log only after the game renamed it to *-anterior (the carried
  copy's exit line had ended the next boot after 3 s, an apparent stall).
- [x] Azahar Old, .3dsx BUILD120: two boots; the second records
  `== 02/50 ... CUELGUE O CIERRE` with `registro_cuelgue.txt` (the FATAL
  lines) and completes level 3 at 30.0 FPS.
- [x] CIA120 (53,060,544 bytes, SHA-256 0359dac7...2328c9) installed in
  Azahar Old (80 MB mode, as hardware): `samples=0x141c9e00` (the Luma
  address), castle 1F tour level 20/20, 29.9 FPS, pause p95 56.9 ms; libc
  heap free 11.3 MiB at entry, 10.3 before the pause, 5.6 after it (second
  cache way for engine B only). LAN QR delivered (download hash verified).
  Physical installation not verified here.
- [x] Azahar Old, .3dsx BUILD117: castle 1F tour level 20/20, 29.9 FPS, pause
  p95 56.8 ms, heap free 23.3 -> 18.6 MiB across the pause (96 MB mode).

## Candidate BUILD114 — sm64ds-enhanced techniques + full-level test tour, 2026-11-07

Owner request: apply the useful sm64ds-enhanced techniques "and more", keep
Old at 30 FPS and New at 60 FPS, and make SELECT+START test every situation
in every map with dumps for bugs, performance and cost.

- [x] Exact VFP divide/square root (`source/native_renderer/ds_math_unit.h`):
  DS divider 64-bit cases (incl. NormalizeVec3's 2^56/|v|^2), the square-root
  unit, `cstd::fdiv` and the native `integer_sqrt` (LenVec3, distances,
  cstd::sqrt). Host: 6,764,840 divides identical to 128-bit references,
  ~10M square roots identical to the restoring loop. ARM11 build vs ROM ARM9
  leaves under unicorn: 5x1043 cases identical.
- [x] Dry updates: the skip is decided before the game draws (late on the
  fixed timeline, or Old's 30 FPS cap in 60 Hz scenes); model/skin/particle
  producers do no work then. At most two updates in a row go unshown; the
  30 Hz intermediate VBlank is never skipped (the old VBlank-counted limit
  could land the forced present on it).
- [x] Top-screen 2D planes: CPU tiles at 1x and the GPU makes the nearest 2x
  copy in VRAM (as the map); CPU 2x fallback when VRAM is short. citro3d
  refuses render targets outside VRAM (first Azahar run: every frame refused,
  "allocation failed"); now Old: back+front on GPU (2.5 MiB VRAM left), New
  with AA: back on GPU, front only if room.
- [x] LZ77 decode without per-byte checks (540 ROM files identical).
- [x] SELECT+START tour of 50 levels (IDs 1-51 except 41, from the ROM's own
  tables), 20 situations each incl. the pause menu, title report, rotating
  character, crash resume; R+SELECT+START keeps the spot test. Host tests.
- [x] Azahar Old, .3dsx: chord on the title writes the tour state and exits;
  levels 1-2 with the full script: 20/20 reports each, 29.8/29.9 FPS, pause
  menu captured on both screens. All 50 levels with 2 situations each
  (development step limit): 48 complete, 0 crashes; courtyard (3) blocked by
  a persistent player pause lock (fixed: 10 s grace, then verified complete
  at 30.0 FPS); level 31 (wing-cap tower) exits to another level by itself.
  Mean 29.6 FPS. Worst p95 comes from the pause-menu opening (~57 ms).
- [x] Azahar New, .3dsx: Bob-omb 6 situations, 59.6 FPS mean (AA 2x, worker
  core 2), p95 18-24 ms.
- [x] CIA114 (53,056,448 bytes, SHA-256 5d1e6776...27cb21) installed in
  Azahar Old: Bob-omb tour level 20/20, 30.0 FPS, refused 0, audio core 1.
  LAN QR delivered (download hash verified). Host tests 104/104.
- [ ] Physical Old/New: owner's tour results (`tools/test_mode_report.py`).

## Candidate BUILD113 — hardware test mode + Old-3DS cost cuts, 2026-11-05

Owner request: optimize the native renderer much further after studying the
other ports (Worms 3D, Dusklight, MK64, LTTP, Minish, Star Fox, Virtual Boy,
Doom, FBI CAM) and public ports, then add a Worms-style SELECT+START test
mode that auto-plays the current zone and writes about 20 reports.

- [x] Rebuilt the lost playable pipeline (build/ was deleted on 2026-11-01)
  from thread history: `tools/native_renderer/pipeline/` + RECOVERY notes.
  All 37 re-cooked banks are byte-identical to BUILD 107; model banks come
  from the surviving CIA107 RomFS. BUILD 108 was reserved by the base tree.
- [x] Log: stdout fully buffered (32 KiB) instead of line-buffered. Each
  line was one SD write (FS IPC) from the game or render thread, about a
  dozen per four seconds; now one write per minute or per scene change.
- [x] GPU/DSP publication through `svcStoreProcessDataCache` (clean only)
  instead of GSPGPU/DSP FlushDataCache IPCs (~0.33 ms each on Old hardware
  per the LTTP/Minish measurements); permanent GSP fallback if refused.
  RSF gains SVC 0x53 (and 0x11, already called by the worker log line).
- [x] 2D bridge and map foreground uploads re-tile and publish only the
  8-row bands that changed (shadow copy per plane; contiguous Morton bands).
  Host test: band re-tile equals full re-tile and writes only its range.
- [x] Bridge foreground composition uses an exact per-state decision table
  instead of building and evaluating a 12-field policy per pixel. Host test
  compares 7.6 million combinations against `bridge_policy::foreground`.
- [x] BUILD111: the lower map foreground rebuilds its overlay base only when
  the overlay changes; otherwise each of the two alternating buffers only
  restores the boxes its OBJs covered last time. BUILD110 Old Bob showed
  source2DPrepare ~3.8–4.3 ms per image on the game thread. The authored
  differential (64 publications, moving OBJs, alternating buffers) matches
  the full CPU PPU exactly.
- [x] SELECT+START test mode (`docs/MODO_PRUEBA.md`): chainloaded restart,
  original StartFile/SetRealCharacter return, same-area spot restore, 20
  scripted situations, per-report CSV/summary/BMPs under `prueba/NN/`,
  START cancels, crash leaves "NO TERMINO". Host test simulates a full run
  including a death. Report writing is excluded from cadence and timing.
- [x] 64/64 native renderer host tests (ASan/UBSan) and the audio/save host
  campaign pass; BUILD109 and 110 compile and link for ARM11.
- [x] BUILD110 Old Azahar: Bob entry, 3000 events in 50145.357 ms (target
  50145.339), 0 refusals, stable windows 29.9 FPS. Scripted SELECT+START in
  Bob wrote `prueba/estado.txt` (level 6, area 0, Mario, position) and the
  game exited with code 0 (Azahar does not chainload).
- [x] BUILD111 Old Azahar Bob, same route: source2DPrepare 1.3–2.0 ms per
  image (110: 3.6–4.3 ms); 3000 events in 50145.357 ms, 0 refusals.
- [x] BUILD112 fixes the course-intro dialogue during the character switch
  (111 stopped as "no se pudo cambiar de personaje"); BUILD113 shortens the
  status strip to fit 256 px. Old Azahar test mode from the chord state:
  COMPLETA 20/20 reports, exact spot restored, 29.9 FPS mean, worst p95
  41.0 ms (running while turning the camera), all report BMPs correct.
- [x] CIA113 (53,048,256 bytes, SHA-256 `ffbfa2dd…df4fc`, title
  000400000F536500) structurally verified; installed in Old Azahar it also
  completes the test mode 20/20 (worst p95 40.1 ms), no FATAL/SVC refusal.
- [x] LAN delivery: HTTP/1.1 server (`tools/native_renderer/lan_server.py`),
  QR decoded to the exact URL, download SHA-256 matches. Mac must stay awake.
- [ ] Physical Old/New: owner test-mode runs in about ten zones; no FPS
  improvement is claimed before those hardware reports.

## Candidate BUILD107 — Old30 / New60+AA, 2026-10-31

- [x] Clone and inspect sm64ds-enhanced commit9d52d999 and reconstruct all79
  patches against its pinned upstream. Transfer compatible techniques, not
  its GX renderer, mutable VRAM races, or15Hz map compromise. Keep our UI.
- [x] Old caps publication at one image per two original display events and
  disables interpolation. New enables interpolation and800x240 horizontal
  supersampling for geometry. The interface remains at its own resolution.
- [x] Fixed logical deadlines, nonblocking normal worker polling, Old game
  priority above rendering, explicit diagnostic/HOME/sleep pause exclusion.
- [x] PICA map enabled by default with CPU fallback for unsupported effects.
  Its foreground upload falls786432→196608bytes; GPU performs sharp scaling.
  Authored PICA lab105:60cases /4608000LCDpixels, zero differing pixels.
  CPU PPU differential:3145728pixels.63host checks plus affected tests pass.
- [x] Audio chunks128→64samples; native rate, gain and buffer durations stay
  unchanged. ASan/UBSan audio/save campaign passes, including generated NDSP
  ownership, restart and unchanged output duration. SWAR preparation was
  already outside the mixer lock in this backend.
- [x] Sequential final107 Old/New Bob runs, fine timing0 and no automatic
  readbacks:3000original events each, elapsed50145.356ms vs target50145.339ms,
  zero refused publications. Stable final120-image windows: Old29.92FPS,
  p95/p99/max35.768/36.911/37.965ms; New59.83FPS,
  p95/p99/max19.532/20.128/20.285ms. These are emulator presentation intervals.
  Across cold-load windows rates fall to25.48Old /47.78New; not fixed30/60
  acceptance. No2x total-cost or physical performance claim.
- [x] Injected80ms renderer delay (106 Old path unchanged in107):1200logic
  events/updates in20058.153ms vs target20058.136ms,207images, zero refusals,
  zero clock rebases/discarded ticks. Ordinary logic did not wait for render.
- [x] Predecessor105 Old /106 New star→Adventure→selector→intro→game journeys
  complete6500events in108648.25ms. These are emulated predecessor journeys.
 106's extra after-deadline endpoint poll regressed Bob cadence and was
  removed from107. Remaining CPU2D and synchronous loads can still hitch.
- [x] CIA107 packaged with original native banks in RomFS; Old80MiB/New124MiB
  policy and CIA/TMD/NCCH/ELF checks pass. Separate native-test save/log.
- [x] Installed-CIA107 Old: star, Adventure, selector, full intro, Yoshi and
  movement. Installed NCCH matches verified CIA content. Test process stopped.
- [x] QR decoded to exact LAN URL; downloaded53027776byte CIA matches
  SHA2566d4e80ade9098a1ef1cb9602a552b67870eb40ae4c9e9178205a00ab0fd80761.
  Only the verified CIA is served; no public upload. Mac must remain awake.
- [ ] Physical Old: install, Adventure, normal music speed, sustained30FPS,
  menu/Bob frame-time spikes, HOME/sleep resume. No mounted SD at packaging.

Evidence: `build/native-renderer/hardware-report91/profile107-validation.json`,
`playable-r1/profile107-campaign.log`, `playable-r1/hardware/build107/`,
`docs/3DS_PORT_RENDER_RESEARCH.md`. The full native-producer migration and
physical performance goals below remain open.


## Active priority — decouple logic/presentation, 2026-10-30

The owner explicitly prioritizes preserving game/audio speed when images are
late, removing render waits from the normal game loop, and reducing CPU2D.
BUILD95 is rejected (owner screenshot8FPS). BUILD96 improves the failed audio
affinity experiment but still has slow intro windows; neither is a handoff.

- [x] Identify cycle-clock rebasing and unconditional worker waits. Native
  deadlines now stay on the original rational timeline; normal publication
  polls one in-flight job, drops an unavailable image and never queues stale
  frames. An obsolete interpolation endpoint releases its own sealed store.
- [x] An injected80ms worker delay in internal98 completes1,200 original
  display events/updates with191 submitted images,1,009 dropped publications,
  zero refusals, zero clock rebases/discarded ticks. This is emulator fault
  injection, not physical acceptance or a measured2x gain. Native elapsed-time
  reporting was added afterwards and still needs execution.
- [x] Original game/input ticks outrank rendering on shared core0; New's normal
  render worker stays on core2, audio on core1. Native pacing stops busy-spinning
  for the last1.5ms. Deliberate diagnostic capture pauses shift the deadline
  explicitly instead of fast-forwarding the game afterwards.
- [x] Attribute the intro's CPU2D work: the diagnostic ENGINE_B window around
  frame2580 spends1,171.344ms in BG and432.581ms in OBJ across69 preparations;
  full BG rasters account for1,038.219ms. These are diagnostic wall scopes,
  include overhead/preemption and must not be added to their nested totals.
- [x] Implement an immutable map/foreground publication and a PICA map pass.
  Only occupied semi-transparent OBJ pixels sample the map on CPU for exact
  six-bit blending. Unsupported windows/effects retain explicit fallback.
- [x] Authored differential:3,145,728 pixels match the full CPU PPU, including
  scrolling, rotation, wrapping, ordinary/affine/bitmap OBJ and supported alpha
  targets.63 sanitized host checks pass before the latest alpha extension;
  its affected differential/preparer tests pass separately afterwards.
- [x] Authored PICA map laboratory completed in103b and105: orientation,
  affine sampling, borders, sharp filtering and resource retirement pass.
  All runs were sequential; no foreign Azahar process was stopped.
- [ ] Run full Old intro and Bob-omb CPU-map/GPU-map comparison, then sequential
  Old/New regression. Report native logic cadence as well as presentation
  intervals, p95/p99/max and frame-time spikes.
- [ ] Package/verify the next CIA only after graphics/cadence checks pass.
  No95/96/100/101 CIA has been copied to the owner's SD during this work.
- [ ] Physical Old: Adventure without a visible stall, normal music/game
  speed, sustained menu and Bob-omb performance.60FPS and2x remain open.

Private evidence: `build/native-renderer/hardware-report91/`, including
`bob96-vs91-old-emulator.json`, `intro95-vs96-old-emulator.json`, preserved
physical91 RAM/logs, and playable session `worker98-delay-menu-old`.

## Latest physical feedback — BUILD91, 2026-10-30

- [x] Owner confirms the opening star is now visible on the Old3DS top LCD.
- [x] Preserve and analyze the actual RAM dump, log, semantic capture and
  1,024-frame timeline. All3,239,176 executable RAM bytes match CIA91's ELF.
- [x] Read actual audio backend/PCM/counters using the matching ELF's DWARF;
  zero observed full drains does not exclude an audible fault or an underrun
  during refill. RAM/DSP/audio are not an atomic snapshot.
- [ ] Adventure transition without a visible freeze, including a two-second
  stall. The owner powered off within a minute; permanent deadlock is unproven.
- [ ] Stable menu performance and correct game/music speed on physical Old.
  The dump confirms57.29→40.71FPS and presentation intervals up to96.693ms.
- [ ] Physical acceptance of resource preloading and native cadence recovery.
- [ ] Required2× frame-cost reduction, complete native producers, and physical
  Bob-omb Battlefield performance. Emulator success does not close these.

Current evidence and limits: `NATIVE_RENDERER_BUGS_2026-10-27.md`, section
"Estado físico vigente — BUILD91"; private `hardware-report91/analysis.json`.

Active goal, latest owner instruction (2026-10-29): make the entire game
native to 3DS. First complete every 2D producer and its GPU composition,
remove the PPU bridge, then eliminate remaining DS translation dependencies
in graphics and logic. Menus, dialogue/scroll, HUD/maps, windows, alpha,
brightness and transitions are explicit acceptance areas. ARM11 assembly
optimization remains part of this goal, with measured benefit required.
The owner subsequently authorized a new private test CIA and installation QR
on 2026-10-29 after seeing the visual improvements. BUILD52 is reserved for
that hardware candidate; this authorization does not close the migration goal.
The goal remains active; existing unresolved bugs and Old/New60FPS targets
remain required. See [native budget and current migration](NATIVE_60FPS_BUDGET.md).

BUILD50 fixes the native upper file selector: overlay7 mode6 now publishes
the menu's MAIN OAM/backdrop on the correct physical LCD, along with all six
known presentation modes. Unsupported cells no longer discard later valid
cells in diagnostic partial capture; incomplete coverage remains reported.
The actual Old-model Azahar selector was inspected with its upper castle
image, text and both arrows present. BUILD51 moves ordinary Message panel
brightness/clipping to a PICA scissored surface quad and removes those owned
window controls from the local PPU view. Retained glyphs and scroll remain.
Old Bob-omb A/B still presents about56FPS: panel ownership reduces bridge
work from about3.45ms to2.93ms and stable-window p99 intervals from about38.2ms
to33.0ms, but does not achieve stable60. Lower screenshots match exactly;
upper panel/cursor fringes differ from the old linearly filtered bridge.
51 sanitized host checks pass. New-on measurement was stopped when the owner
requested packaging; New-off comparison remains pending. No hardware result.

BUILD52 enables native menu2D and the ordinary native Message window by
default for the requested test. Explicit diagnostic overrides with value0
retain the comparison routes. Dynamic menu text/canvas, affine cells, star
recolour, remaining HUD/maps and effects still require native producers;
the PPU bridge is still used outside the owned native menu/window routes.

Requested hardware handoff: BUILD52 CIA packaged (52,990,912bytes), exact ELF
and CIA/TMD/ExeFS integrity verified,1050native banks included. Installed CIA
content matches the verified NCCH; New-model Azahar title and file selector
visually checked with the upper image present. Local FBI QR decoded and its
HTTP download SHA-256 matched the CIA. Evidence:
`build/native-renderer/playable-r1/hardware/build52/{integrity.json,azahar-installed-validation.json,delivery/receipt.json}`.
CIA SHA-256: `b6b7b155e1021da5e19d7a800800888875e798115993df35cdef41da847dd29e`.
Physical installation and performance remain pending; migration goal active.

Hardware launch regression, BUILD52 → BUILD53: the private packager reused
`qr-test/native-test.rsf`, which still requested Old96MiB/SpecialMemoryArrange
instead of the maintained Old80MiB policy. BUILD52 structural/code checks
missed that launch contract. A332byte Luma dump found in Downloads identifies
HOME Menu (`menu`,0004003000009802), data abort at0012c718, null destination
for `strh r3,[r1]` while initializing a heap span of0x00d00000. Its code does
not match the game ELF, so game symbol names are inapplicable. The owner
reports the crash immediately after the Homebrew splash; dump identity is
probable rather than independently proven. This evidence is consistent with
the96MiB application's12MiB SYSTEM region being insufficient for HOME.

The maintained `tools/package_native_renderer_hardware.py` now derives the
native-test RSF from `packaging/sm64ds-integrated.rsf` and verifies the exact
ELF and complete launch policy using `verify_integrated_cia --native-test`.
Only the private test identity and required RomFS differ. Old80MiB/New124MiB,
804MHz/L2/core2 and the existing restricted access policy are checked. BUILD52
is rejected by this verifier; BUILD53 passes.52sanitized host checks pass,
including a policy regression test rejecting96MiB. BUILD53 Old-model Azahar
installed CIA reaches title and selector with the upper castle image visible.
New-model Azahar installed CIA also reaches the interactive star; the exact
installed content hash matches in both sequential runs. Direct emulator boot
does not exercise the physical HOME Menu allocation/reboot path. The original
BUILD52 LAN server was stopped; the new QR serves BUILD53 only.
No physical fix or stable60FPS claim is made. Private evidence is under
`build/native-renderer/playable-r1/hardware/build53/`.

Current checkpoint BUILD44: ordinary Message glyphs and four-pixel scroll use
retained native atlas quads. The variable-position edge regression reported
in BUILD43 is corrected and verified against 1,536,000 pixels per emulated
model, plus the actual reported Bob-omb phrase/scroll. 48 host checks and the
sanitized dialogue check pass. This does not migrate Message window effects,
the rest of its fonts/entrypoints, menu/HUD/maps or the entire game. The menu
30FPS report stays open. Private evidence:
`build/native-renderer/playable-r1/dialogue-build44-validation.json`.
No CIA was created. The next renderer change must target menu producer and
composition coverage; detailed interval accounting now includes lower-LCD
geometry and separates source2D preparation in source awaiting a candidate.

Next OBJ checkpoint: all six menu cell banks are prepared as20alternative
layout/engine/language views (50cells each). Native C++ parsing and semantic
selection validate all1,000images with ASan/UBSan;50host checks pass. The
contracts and complete generated catalog also compile for ARM11. Playable
loader routing, remaining menu backgrounds/effects and GPU parity are still
pending. Evidence: `build/native-renderer/menu-obj-r1/validation.json`.
No new candidate/CIA; this is resource and selection coverage, not menu FPS
acceptance. See [complete OBJ preparation](NATIVE-MENU-2D.md).

OBJ loader connection: `020bcba8/020b3f20` now select/validate complete native
views and cell draws use committed per-engine bindings. The maintained
preparer accepts `--menu-obj-catalog`; the staged ARM game links the full
catalog and passes linked ABI checks.50host tests pass. The native2D gate
remains closed in playable mode until menu backgrounds/effects and placement
coverage are complete; this does not establish runtime/FPS acceptance.
Evidence: `build/native-renderer/menu-obj-r1/loader-validation.json`.

Static backdrop checkpoint: all four embedded maps are prepared as11native
canvas banks/52palette-language states. Native NMD parsing passes with
ASan/UBSan; the original isolated PPU matches5,111,808source pixels exactly.
50host tests pass. Source backdrop colour remains separate from transparent
pixels. Live background routing, dynamic text/canvas maps, fades/windows and
the newly identified SUB OBJ star recolour are still required before opening
the native menu gate. Evidence:
`build/native-renderer/menu-backdrops-r1/validation.json`. No new CIA.

Backdrop connection: all11views now feed native canvas placements/clips and
explicit palette0 backdrop layers through `MenuBackdropState`. Original
load/crop/palette/publication seams are connected; the maintained preparer
accepts `--menu-backdrop-catalog`.51sanitized host tests pass and the staged
ARM game links with valid ABI. Evidence:
`build/native-renderer/menu-backdrops-r1/state-validation.json`.
Next: exercise an opt-in native menu3DSX route with the prepared banks,
collect actual placement/effect gaps, and finish the menu composition.
Playable's default PPU bridge, dynamic text/canvas maps, star recolour,
fades/windows/alpha and wider background coverage remain open. No CIA.

Latest objective, 2026-10-29: optimize shared native routines with ARM11
assembly on both Old and New 3DS. Start with exact Fix12 affine matrix
composition; compare against C and the original routine, then measure the
same executable with ASM enabled/disabled in Bob-omb Battlefield. Retain
frame-time spikes, presentation intervals and separate hardware acceptance.
See [frame budget and ARM11 work](NATIVE_60FPS_BUDGET.md).

Latest request, 2026-10-27: the active native-renderer bug list and work order
are in [Render nativo: errores reportados y lista vigente](NATIVE_RENDERER_BUGS_2026-10-27.md).
It includes the new fullscreen dialogue bug, white file-selector background,
all earlier user reports, and explicit regression checks. Earlier acceptance
entries below do not close these reports on the new renderer.

Current consolidated inventory (2026-10-21):
[Inventario del port y lista de trabajo](INVENTARIO_PORT_2026-10-21.md).
It reconciles the historical entries below with the current source and separates
implementation, recorded acceptance and remaining verification.

Current request, 2026-10-23: development and performance work resumed. Focus on
Old 3DS, complete diagnostic bundles and Display > Lock FPS (Off/30/60; Old
defaults to30, New to60). The physical target remains stable60 on both models.
See [current dump evidence and limits](NATIVE_RAM_DIAGNOSTICS.md) and
[the corrected timing interpretation](CODE_PASS_2026-10-22.md).

- [x] Candidate c2451eea built/audited; Old-model Azahar run closed with three
  timed and three directed dumps. All six include complete6MiBVRAM and valid
  top/bottom LCD scanout, with matching context and closed-receipt hashes.
- [x] Actual Display page inspected: default30, selection60, reopen retaining60,
  selectionOff, save/resume and ordinary-HID dumps. Bottom LCD captured before
  the progress page; UI screenshot files preserved in the acquisition.
- [x] Final candidate6788c4dd includes cadence-aware automatic dumps. Old/30
  reference closes in97.35s with3complete LCD/VM/VRAM bundles and0automatic
  captures; detector regression distinguishes ordinary33ms from real stalls.
- [ ] Physical acceptance, complete routes, broad interpolation regression and
  valid per-function CPU sampling remain open. These checks do not close them.

Historical note, 2026-10-18 (superseded by the current request): physics/performance
work was paused to prioritize HUD/camera/Options. The historical entries below
retain their original candidates and evidence; they are not current acceptance.

## Latest request — distinguish submenu actions from changing values

- [x] Options categories now use full orange TOUCH TO START-style action buttons, with category names inside and no separate OPEN controls. Python copies the original240x34 bevel at1x and extends only the clean interior. Existing value selectors retain their cream frames and green arrows.
- [x] Developer now reads CLEAN DUMPS with an orange CLEAN action.
- [x] Follow-up: Dump RAM now has original green arrows to toggle capture permission. They never request a capture; A/the centre retains the capture action. Enabled value is L+R+A, with Python-authored7x7 plus sprites on the original1x pixel grid. Full Dumps mirrors the same existing permission flag.
- [x] Confirmation displays DELETE DUMPS? and horizontal KEEP / DELETE actions. Removed redundant OK; Back cancels. Shared geometry governs drawing and touch. Left/right chooses without activating; A confirms. Regression covers both actions, Back, gaps and retired hit boxes without deleting files.
- [x] Native model and bounded artwork tests pass ASan/UBSan; artwork and all-page render compile with Wall/Wextra/Werror. Reviewed four changed pages at native320x240 and integer enlargement. Source previews: assets/proposals/options-action-buttons-2026-10-18.
- [x] Candidate fbdc5381c5b977da compiled/audited and ran in New-model Azahar for187.6s, no recorded emulator fault. Actual full Options pages, CLEAN question, inert gap, KEEP/Back return, left-half category touch and OK/title/wrench reopening observed; screenshots saved beside the source previews.
- [x] Follow-up candidate a2a557f5b08a5050 compiled/audited. New-model Azahar attempt1792358115954381000 closed normally after174.6s with no recorded emulator fault. Right arrow enabled L+R+A, left arrow restored Disabled, neither opened a dump screen; CLEAN/KEEP/Back/OK and wrench reopening observed. Six current full screenshots, unchanged lower-LCD contact sheets and hashes saved in assets/proposals/options-action-buttons-2026-10-18.
- [ ] Physical-console acceptance and intermittent upper-title display regression remain open. Keyboard-right inspection of the preceding candidate overlapped the profile's keyboard-touch mapping; horizontal choice behavior is covered by the host model, not claimed as a physical button test.

## Correction after rejected star/font/paging review

- [x] Source black crops were importing neighbour strokes into glyphs (L beside I). Rebuilt every outline exclusively from its own ink silhouette. All26glyphs have one connected visible silhouette; no detached black marks.
- [x] Original11x12 selection star returned to1x. One original22x15 blue down arrow centered below compact rows cycles pages; green arrows remain beside changing values. Touch geometry updated and tested with ASan/UBSan.
- [x] Both HUD arrows use complete original32x32 frames at1x, replacing fractional40x40 review assets. New visual artifact E-original-HUD-integer-arrows.
- [x] Candidate b097c2802a1c00b0 built and audited. New-model Azahar attempt1792356750522337000 opened all12menu views, exercised blue paging, volume touch100to155, OK/title return and wrench reopening; no recorded emulator fault at bounded close. Actual screenshots and contact sheets in assets/proposals/options-clean-pages-2026-10-18.
- [ ] Physical-console UI acceptance remains open. Upper title scene rendered intermittently during options navigation; retain display regression. One automated drag gesture did not alter volume although direct touch worked; do not mark full hardware drag acceptance complete.

## Original menu controls and integer pixels

- [x] Rechecked Mario Universe Star sheets and TSR Menu Elements6855. Selected the original flat menu selection star from the clean magenta sheet; preserved its black outline by removing only the exact magenta palette entry.
- [x] All visible menu controls now use original1x pixels:64x32 complete OK,10x14 green arrows (right mirrored),32x32 back. Selection star uses exact2x blocks (11x12 to22x24). Fixed labels at x44, star at x12 and consistent10-pixel gap. Frames extend only flat interior, and value font uses1x pixels.
- [x] Pixel audit compares each rendered control pixel to its source1x or2x block; passes. Native model and all-page drawing pass ASan/UBSan; every review page regenerated. Evidence:assets/ui/retail-pause/pixel-controls-audit.json.
- [ ] Actual emulator/console acceptance remains pending; generated review pages are not emulator captures.

## Retail label proportions correction

- [x] Replaced the tall 7x12 letter plan with original measured 8x11 faces, 14-pixel raster height, overlapping black outlines and the original white/yellow/gold/salmon palette. Extracted connected ink components preserve transparent counters rather than filling enclosed background areas. Retail BACKLIGHT/SOUND supply 14 original glyph shapes; 12 missing letters are extended on the same grid in Python.
- [x] Regenerated every menu preview; ASan/UBSan all-page rendering and bounds checks pass. Compact horizontal volume remains unchanged.
- [ ] Visual approval and emulator/console acceptance remain open; complete alphabet extensions are not claimed as original ripped glyphs.

## Label font and compact volume revision

- [x] Replaced sliced word fragments and reduced title glyphs with a complete Python-authored outlined A-Z pixel alphabet. Fixed cap height, stems, baseline and shadow; original SOUND/BACKLIGHT artwork remains the visual reference. This is a recreation, not a verified pixel-identical original font.
- [x] Volume now occupies a 40-pixel-high single row with percentage on the right. Touch drag bounds and behavioral regression updated; ASan/UBSan model and all-page render pass.
- [ ] User visual acceptance and actual-console verification of this font revision remain pending.

## Options visual revision — retail title options reference

- [x] Shared options painter now separates outlined option labels from value buttons; retains blue Mario DS pattern without the Mario head. Developer toggles use separate ON/OFF buttons, current selection shaded. Original chamfered golden OK sprite replaces the rectangular footer.
- [x] Touch regions updated for relocated paging, sound arrows, volume drag and idempotent ON/OFF selection. Native menu behavioral regression and bounded render pass with ASan/UBSan.
- [x] Regenerated all 12 review views in assets/proposals/options-all-2026-10-18. These are rendered source previews, not emulator captures.
- [ ] Compile and verify revised options on emulator and hardware; keep all earlier functional issues open.

## Latest priority — original lower HUD proposals

- [x] User approved the chamfered SURROUND-style menu frames on 2026-10-18; retain this design.
- [x] Download original HUD & Minimap Stuff sheet 137713 and generate three 320x240 layouts from original sprites, with original castle map and two square left/right camera buttons. Source/crops/positions and reproducible Python generator in assets/proposals/lower-hud-2026-10-18. These are layout previews, not emulator screenshots.
- [x] User selected original-size A HUD with 40x40 B camera arrows; saved as D-approved-A-hud-B-arrows.
- [ ] Integrate selected original sprites and matching two-button touch regions, retain Old3DS stylus camera, then verify against actual dynamic HUD/game states. This supersedes the earlier request to remove every camera arrow.

## Regressions reported 2026-10-18 — active priority

These reports reopen acceptance; earlier build or screenshot checks do not close them. Latest weekly usage reading: 39% (owner cap: 50%).

- [x] Reproduce title Options open/close/reopen failure on delivered candidate95f2bc484896ada1, New-model Azahar. Wrench opens once, B closes, second wrench touch does not reopen. Original consumed-input latch was never released by the intercepted page.
- [x] Implement generated-source latch release plus neutral key/pen history on modal exit. Five-cycle executable C regression and six hook tests pass; candidate84c0fd998000d7ce builds and audits. Candidate6ff53495d95cd557 also records four modal opens/returns; Adventure and File A touch navigation worked after closing Options. Exhaustive rapid-cycle acceptance remains pending.
- [x] Generate SURROUND-style chamfered gold frames from the exact user-supplied sprite reference in Python; preserve corner pixels, use uppercase original bitmap labels, and enlarge the volume frame without repeating internal borders. Shared title/in-game painter updated; sanitizer rendering test passes. Candidate86acf4cfb28b7b59 built and passed integrated audit. Preview uses the actual shared menu painter on the original tiled background; live emulator/physical visual acceptance remains pending.
- [ ] Restore approved original menu appearance consistently at title and in-game, including original title, frames, selection star and back arrow proportions.
- [ ] Verify repeated title Options open/change/OK/back/reopen and Adventure/file touchscreen navigation without slowdowns.
- [ ] Remove OK flicker and apply display settings while paused without losing the retained level or original map/statistics.
- [ ] Preserve upper 2D sprite aspect ratio in Wide; avoid stretching the upper menu graphics.
- [ ] Native rendering from initial startup and opening cutscene; inspect actual targets and fallback reasons, not just saved labels.
- [ ] Diagnose and correct persistent physical-console audio crackling; distinguish starvation, source discontinuity and gain clipping.
- [ ] Opening intro: first Start shows “Start to omit”; release then second Start ends the opening via original APIs and uses the original white scene transition into playable Yoshi. FSM sanitizer test and candidate6ff53495d95cd557 build pass. In Azahar, first Start showed the hint, second reached playable Yoshi, and movement worked. Fade still needs polish: actors disappear before the white transition fully covers them.
- [ ] Reopen Original/Hybrid/Nintendo64 behavioral acceptance: the user reports only Original works correctly. Specifically reproduce broken triple-jump chaining, stuck locomotion and missing ground dust in Hybrid/Nintendo64. Check movement, animation, particles, collisions and DS abilities; solver ownership alone is not correctness.

## Current user priority

See HUD_CAMERA_FRONT_OPTIONS_REVISION.md. The previously drawn gameplay HUD was rejected; its historic pixel comparisons describe that old implementation, not the desired appearance. Restore original ROM sprites, remove camera arrows, add Old3DS drag camera, and expose shared settings from the original title wrench. Broad physics/performance testing remains paused.

## Completed implementation and inspected evidence

- [x] Clock/Wario failure diagnosed in original ARM saved-RAM replay: ordinary class2 floor, effect17 ceiling2080units overhead. Reachable unsupported ceilings still delegate to DS; distant effects no longer veto native motion. Scene wall cache survives/revalidates temporary DS handoffs; stationary expected refusals retry at bounded intervals with immediate action/state/position retries. Boundary/overflow/retry sanitizer gates pass; candidate0dba8754543e806a builds/audits. Two Clock/Wario captures prove real Nintendo64 ownership,399/955steps, no fault, exact native HUD pixels. Later input left the ledge and triggered original stage transition; third capture is diagnostic only, not a three-sample performance pass. All3newVMs analyzed/retired.

- [x] Main candidate0d4b19cc02ea82c5 builds/audits with independent persisted physics/button modes, actual DS logical commands, CirclePad digital-direction filtering and no lab autoplay. Bob Nintendo64 ownership was captured in4real RAM samples. Original interactions/abilities remain explicit DS handoffs rather than simulated button remaps.
- [x] Main lower HUD now includes12camera templates,13counter glyphs and30portrait/map/cap/marker templates, resident native atlas and correct cross-layer/priority ownership. All25normal-castle fixture pieces covered; NewWhomp3captures show zero differing final native pixels in both HUD inspectors. Original map/castle backgrounds retained. Unknown/effect states retain guarded original fallback; broad/hardware acceptance remains open.
- [x] Whomp exact degenerate prism diagnosed from original ARM RAM replay: integer determinant classification retains all516wall entries and delegates only nearby ambiguous contact to DS. Full-cache original-ARM regression plus FMA fast/off sanitizer tests pass; OldWhomp Hybrid/Dual-hand completes4real RAM captures with native owner true and no fault; native pixels/allocator checks pass, all4newVMs analyzed/retired.

- [x] Production integration now links the real N64 controller/DS-world backend. Controller has independent Buttons and Control submenus; Original/Hybrid/Nintendo64 persists under input.physics without changing DS button mode. Candidate236b32fb0bef4c9b Old castle run boots/resumes both native modes; two extra RAM captures prove Hybrid/N64 own displacement, finite state, no latched native fault. Five captures analyzed and retired. Yoshi held-jump/action exercised; broader characters/interactions/levels remain acceptance work.
- [x] Diagnosed real idle/wake hang with Azahar Wait Tree PC0x39880c: stop_sound walked uninitialized N64 mixer lists. Physics-only core now guards stop operations like play_sound. 1800-idle-update wake/walk/jump regression and420 original-ARM-backed wall/floor solver steps pass. Expected DS effects/unknown friction refuse a staged native tick without changing caller output; session closes/reset cleanly.
- [x] Added13native counter assets: ten10x20numbers and20x20coin/star/multiply, explicit Python integer contours and1:1painting in shared cached native HUD overlay. Exact original encoded/palette admission, priority/occlusion checks, invalidation and clipping pass ASan/UBSan, including actual captured original OAM fixtures. Candidate0f8a75437d97cd8b New Bob run completes3captures; final pixel audit pending at this update. Portraits/map markers and other HUD states remain open.
- [x] Bob scene readiness failure was traced to two exact duplicated moving-collider registry pointers, not guessed capacity. Canonical entries contain953triangles total. The adapter now validates both alias/canonical live identity, enumerates geometry once, and retains original-query contact validity. Three controlled offline original-ARM alias/stale-owner checks pass with16376query calls. Candidate7ab8ea163ead9dcb built/audited; live Bob acceptance in progress.

- [x] All12camera templates now recognized (left/right/zoom, large/small, normal/pressed) from original RenderSub executed in offline saved RAM.13unique exact byte identities; pressed gold arrows preserve the frame, small controls40×40and large40×60. ASan/UBSan validate all12groups and held/released image-key changes. Candidateebc57e0e12111ceb builds/audits; actual Standard→Touch→Dual-hand selection/OK/resume works through real200ms touches. Old3capture run has exact3712normal,3712pressed,944Touch-zoom pixels, zero mismatches. Dual-hand small appearance/basicCirclePad observed, no additional dump captured. All3rawVM analyzed/retired. Remaining HUD/fade/other-scene/physical acceptance open.

- [x] Native normal camera HUD is integrated: exact encoded/palette bytes and four-piece geometry identify each group; only proven frontmost groups suppress their16old sprites. New320×240overlay retains transparent map interiors, caches unchanged images and reduces25DSsprite draws to9plus1native draw. Oldcastle21c4eeb0 and NewBob754d93d2 complete3RAM captures each; scene/position/allocator checks pass. New `.lower` synchronized game-target readback avoids dump-progress contamination in legacy `.render`; all3Bob captures match3712opaque native pixels exactly. Pause/upper statistics/resume inspected in Old. Pressed variants/other HUD/physical checks remain pending.

- [x] Native HUD foundation: Python authors14transparent pixel assets (camera arrows, normal/pressed40×60frames, star and multiply), with integer1:1 C++ painter. Offline original-ELF-matched RAM decoding under ASan/UBSan identifies25lower OAM sprites and4camera groups, each split across4DS objects. Normal camera groups are now integrated and pixel-verified below; remaining HUD/pressed variants stay open. Original portraits/map/priority/brightness remain intact. This does not close the all-HUD requirement.

- [x] Instrumented complete candidate43f004df3b3bd639 builds/audits; Old-model castle/Yoshi run completes3RAM captures with actual400×240 geometry and0.8wide projection, distinct sampled positions and real CUA movement/jump. Queue profiling separates synchronized begin7.880ms from unpaced begin0.003ms and explicit-flush end0.322ms. No synchronization removed. Lab lower-panel dirty caching removes per-update repaint/publication; measured composition means18.402/16.227→1.911/1.929ms in two interactive sample pairs, with valid owner/position/allocator reports and responsive mode/character/ability UI. All5new rawVM files analyzed/retired with receipts. Physical FPS and final ten-scenario matrix remain open.

- [x] Two real Old-model lab RAM captures complete with visible DUMPING RAM progress and resumed movement. Exact ELF code (2901952bytes), runtime anchors, active native owner/actor and DS↔solver positions validate in both Mario N64 and Luigi Hybrid. Allocator reciprocal links and linear/VRAM lists pass. New reproducible audit retains reports/trace and retires only these two analyzed .vm files (228673632bytes); user dumps untouched. Not physical crash/FPS or final campaign acceptance.

- [x] Old-model interactive lab verifies Original CirclePad, Hybrid Luigi/Yoshi DS handoffs and N64 Yoshi empty-target action/return. Found and fixed phantom re-jump caused by losing held-button history on native recreation. Actual Controller history seeding preserves new presses;1022session steps and420saved-world regression steps pass. Follow-up Old UI log proves23grounded native updates with B still held, followed by a valid new jump; no fault. Full abilities/progression/scene and physical performance remain pending.

- [x] New separate lab links the native DS-world backend and releases native ownership for DS delegation/scene/control changes; Wario table ratios apply in both native modes. Immutable3DSX75f09493ELF/a2bb48caimage runs in isolated New-model Azahar:960scripted presentation steps,1016logged native ticks, four-character clip/foot/voice API checks and nonzero submitted PCM pass with no adapter fault. Main Controller/Hybrid/DS abilities/other scenes/hardware remain pending; mainc8126d84unchanged.

- [x] Unified native DS backend now executes with the actual host N64 solver and original ARM queries in a fresh saved-RAM copy:420steps(180walk/jump+240directed walls), eight wall positions,612filter calls, no bridge errors; one no-floor spawn is explicitly not executed. Per-tick wall refresh preserves every recorded result while reducing registry checks72336→10320and geometry reads13297→2961. Frozen-world CPU evidence only; main-game/scene/ability integration and real FPS remain pending.

- [x] N64 core carry provider now routes registered DS platform transport with staged position/yaw and no legacy fallback on error; unregistration restores original path. Stable32-handle owner costs2320ARM bytes and preserves pinned/replaced identities. Session gate passes960ticks including120platform steps and rejects missing carry ownership. Unified DsWorldSource callbacks compile on host/ARM(183720resident bytes); saved-world execution is now recorded below; production/hazard/scene integration remains pending. Weekly use21%.

- [x] Added original-normal/plane/material surface descriptor:1243ordinary captured contacts pass,2water contacts excluded; moving handles/effects/unknown friction are explicit, failed outputs unchanged. ARM build and ASan/UBSan pass. Original virtual platform transport matches six controlled transforms exactly,12stale references rejected. Fixed pinning of nested floor/ceiling/wall/object-floor transforms even without a direct active platform; actual-instance ownership and840session ticks pass. Stable platform owner/core carry hook/main activation still pending.

- [x] Native prism enumeration matches1245saved triangles; original DS actor/CLPS filtering passes12289ARM cases including wall-only effect0x14/bit0x40. Added full-registry uncached wall reference and bounded preload cache:451entries,181392ARM bytes, ten cached queries byte-identical to reference, four geometry refresh/enumeration reads versus1245(excludes hit revalidation). Rotated moving floor-to-wall refresh and stale/capacity/fault staging pass. Still needs scene hooks, material/platform ownership and main-game activation.

- [x] Added fractional DS-triangle N64 wall narrow phase.37800actual imported-engine comparisons agree(3774hits), including both projection directions, boundaries/radius cap and a six-wall corner with first-four retention. ASan/UBSan instrument test/original collision TU/surface loader; ARM syntax passes. WorldSession now uses this kernel in its840tick synthetic-world gate. Native candidate enumeration/pass-through/material/platform integration remains pending.

- [x] Added live KCL prism snapshot with registry/owner/material/index validation;1245saved triangles and26stale/malformed rejections pass ARM replay. Fractional world conversion passes1254captured/controlled records under ASan/UBSan plus ARM syntax check, including six transforms and three extra-Y-scale cases. Still unlinked from production movement; real wall/platform/gameplay wiring remains pending.

- [x] Diagnose submitted physical dump crash: incomplete VRAM payload and matching FS abort register. Preserve user's original artifacts.
- [x] Bounded64KiB ordinary-RAM staging before FS writes; include mapped VRAM rather than silently excluding it. Integrity/error tests pass.
- [x] Native lower dump progress card, no per-progress allocations or graphics ownership changes. Real L+R+A and complete VRAM payload observed in Azahar.
- [x] Native320×240 root pause and current Options/Controller/Audio/Display/Developer/Update/confirmation artwork. Original game state and input remain the owner.
- [x] Stop custom pause glyph/frame writes to retail VRAM. Native UI removes only EngineB BG0/BG1/OBJ from the PPU's local view; retain map BG2/BG3 and actual register bytes.
- [x] Two real PPU fixtures: unchanged default output and repeated replacement off/on/off match original masked-reference pixels under ASan/UBSan.
- [x] Real-game pause/Options/Audio/Update returns observed without garbage tiles, original map and upper level/mission/score text preserved (candidate64d51b).
- [x] Larger Audio panel, fitted percent and native slider/sign pixels. Real touch reached100/195/200; default100 and range0..200; fixed-point saturating mixer implementation retained.
- [x] CirclePad analog applies to all three original DS button modes, retaining button bytes and real stylus priority; host tests cover all modes.
- [x] Actual RAM proves resumed Original256×192 versus Native400×240 Wide. Paused geometry remains held until resume. Source now displays target dimensions and an apply-on-resume hint.
- [x] Fields keep a stable body font when values change; root/category titles retain colored game glyphs.
- [x] New Bob/Mario scenario completed3automatic captures, real movement/jump/camera input,3additional explicit resolution captures. All6allocator lists validate, rawVM retired after analysis. Reports and GPU/trace evidence preserved.
- [x] Checkpoint474bc61480b1457a builds/audits/packages; Old castle/Yoshi Buttons inspection is recorded separately from64d51b New Bob UI inspection. Immediate upper-diagram preview refinement builds/audits in7183d9ec65167a5a; its live transition check is pending.
- [x] Optional N64 floor-provider routing builds for host and ARM; the CPU differential closes the earlier one missing hit and five large height differences across951 original-DS ARM floor queries. Unregistered behavior remains the existing lab mesh. This is not production movement acceptance.
- [x] ARM world-ground bridge invokes the original24-collider registry with actor exclusion, original CLPS words and world normal/height/contact identity. All24slots and951differential queries pass; no allocation/I/O in the bridge. Moving-platform geometry and stable contact lifetimes remain open.
- [x] Integer-world N64 floor admission shim includes its78-unit allowance, exact equality boundary and-11000lower limit. Eight flat-floor boundary cases and four overflow/lower-limit rejections pass on ARM; slopes and movers still need directed semantics tests.
- [x] Extended near-contact gate:2067queries over234sloped castle triangles pass; total static bridge gate4015ARM calls.
- [x] Replay saved physical-capture state with byte-exact executable/anchors. The two moving-mesh triangles are water, correctly excluded from ordinary ground. Added a separate water-query bridge; both triangles agree with the direct original query.
- [x] Original Transform replay checks translation, quarter yaw and pitch on both water triangles (six cases), preserving world normals; measured fixed-point ray-height error remains below0.107world units. This is a controlled offline fixture, not physical platform gameplay.
- [x] Added original world segment-query bridge;20real castle ceiling contacts agree with direct queries, and four degenerate/overflow segment inputs leave output unchanged. N64 ceiling admission and wall-cylinder behavior remain pending.
- [x] Added bounded stable surface storage:256slots cost30736bytes on ARM; no allocation/I/O while acquiring contacts, no copying/moving the owner, and every retained/current-tick contact is protected from eviction. ASan/UBSan pass10000churn ticks with50000new contacts and three pinned old contacts. Controller contact enumeration and production integration remain pending.
- [x] N64 core now exports actual retained-contact enumeration for all live instances: floor/ceil/wall/object-floor and platform, without changing bound solver state. Host/ARM builds pass; pins recognize the permanent water pseudo-floor and reject arbitrary foreign geometry. Two real headless instances and360jump/churn ticks pass with modified headless/ownership code sanitizer-instrumented. Main-game wiring and physical acceptance remain pending.
- [x] Long-ray diagnosis found32-bit plane-product overflow and Q12 ratio precision loss in the adapted raycast. Generated-copy fix uses wide products and a high-precision path when overflow/segment length requires it. Twenty100000-unit ceiling rays now match original prism-plane tolerances (maximum0.044305world units), while tested short/water/transform outputs remain identical to the original fixture. Full integrated candidatec8126d8431f00a80 builds/audits with frozen source evidence; emulator/hardware regression remains pending.
- [x] Added DS-world N64 ceiling admission query withY-78boundary,100000sentinel and owner-validated plane refinement. Sixty near-boundary cases across20real castle ceiling faces and six empty/invalid cases pass in ARM saved-capture replay. Generated N64 core ceiling-provider entry builds for host/ARM; actual find_ceil hit/miss/unregister checks prove no fallback to the old mesh. Main DS-world callback/cache wiring remains pending.
- [x] Added exclusive WorldSession ownership of N64 instances/callbacks/cache. It pins contacts before create/tick, resolves source identity/properties into stable records, stages output on failure, and deletes solvers before teardown. Synthetic-world840tick gate covers two actors, wall stop, low ceiling, raw CirclePad adjustment, resolver failure/failed creation and reopen/destruction under the stated sanitizer scope. Real DS wall/CLPS/platform sources and main-game wiring remain pending.
- [x] Added N64 wall-provider entry with staged correction validation: malformed/NaN/vertical movement/invalid contact-count results preserve caller coordinates and do not fall through to a mesh. Host/ARM core builds and sanitizer component gate pass.
- [x] Added ordinary CLPS locomotion profile: six DS friction classes map to N64, preserving class5slide terrain, raw words, sound/parameter/filter fields and class1latch metadata.24582DS-vs-N64 normal/class cases and1850original-ARM getter comparisons pass. All23captured entries decode(22solid/1water); DS effects/unknown classes are explicitly excluded from ordinary mapping and still need gameplay delegation.
- [x] Fixed scaled-Y contact-plane origin in the adapter: SclY's inherited triangle-origin method omits its extra scale, so plane refinement now uses the original collision transform. Three controlled offline cloned-SclY cases(0.5/1/2scale) pass; actual stretched-platform gameplay remains pending.

## Still being implemented or awaiting acceptance

- [ ] Physical regression of the VRAM/FS dump crash on the user's actual Old/New consoles. Emulator captures do not close this.
- [ ] Complete broad lower-HUD acceptance beyond the implemented camera/counter/portrait/map/cap templates, including effects/occlusion and uncommon game states. Keep original guarded fallback and map/castle backgrounds.
- [x] Controller Buttons submenu for DS Standard/Touch/Dual-hand; nested acceptance/cancellation tested, actual Touch movement verified in Old-model Azahar (474bc614).
- [x] Independent Control submenu and real movement physics Original/Hybrid/Nintendo64 implemented and captured in the main Oldcastle executable. Broader DS button-mode/other-scene/physical acceptance remains pending.
- [x] Integrate real Hybrid/N64 movement solver into the main game beyond the isolated lab; one movement owner and original DS ability/interaction handoffs.
- [ ] Complete broad real-game acceptance for characters, progression, stage transitions, moving geometry, transformations, animations/audio and physical consoles.
- [ ] Complete the collision adapter: retain N64 floor admission semantics (including its78-unit allowance), wall/ceiling queries, stable surface storage and moving-platform ownership before registering it in production. World-query routing alone is insufficient.
- [ ] Keep C-Stick sensitivity and CirclePad sensitivity visible; remove user-facing debounce/deadzone/vertical-speed clutter. Verify actual horizontal/vertical camera angular response in the real game, beyond symmetric host math.
- [ ] Full directed Developer/confirmation/Controller visual and touch/keyboard return regression with native pixels; confirm no reappearance of discarded blue options UI.
- [ ] Live Select diagnostics: retain movement/B/camera, close only with Select; complete distribution/information polish and actual input regression on the final binary.
- [ ] Physical initial-star black/gray backdrop regression; previous emulator/GPU evidence was black, user's physical report remains relevant.
- [ ] Sustained4:3 black margins across scene transitions and effects; retain Wide horizontal culling correction and check visible actors at both edges.
- [ ] Validate Native/Wide on real gameplay/effects and defaults. Preserve feedback paths when required; explain actual fallback rather than reversing correct labels.
- [ ] Auditory verification of sound modes and0/100/200 gain on hardware; source/visual tests and muted emulator do not prove sound quality.
- [ ] Final icon/banner acceptance using the user's approved artwork; real HOME Menu visual proof remains separate from BNR/CGFX/CIA validation.
- [ ] Recheck relevant upstream100%-matched decomp changes, importing useful reviewed changes without replacing the pinned adapted dependency wholesale.
- [ ] Final same-binary ten-scenario performance matrix with3captures each, distinct characters/zones and real input. Prior mixed-candidate30capture matrix and the new Bob case do not complete this final matrix.
- [ ] Analyze actual CPU/GPU/I/O scopes, optimize measured redundant work without changing game behavior, retain reports and retire only analyzed new dumps. Do not sum overlapping scopes or equate a VM snapshot with a process profiler.
- [ ] Stable60FPS target on Old and New with Native/Wide: physical acceptance still pending; emulator60Hz presentation or renderer interpolation is not proof of60 game updates or console performance.
- [ ] Final complete CIA/3DSX, private local distribution, short verified link and decoded/hash-checked QR. Existing experimental QR is not the completed release.

## Earlier features to retain during final regression

- [ ] ROM-only setup accepts an arbitrary `.nds` basename under`SD:/3ds/sm64ds/`, with supported-ROM identity checks; arbitrary name does not mean arbitrary game/region.
- [ ] Prepared single-file assets, desktop packer appearance/drag/save flow, six-language console loader and English fallback.
- [ ] Approved centered logo/background; extraction-only long-wait/almost-there text, compact cancel button and single-line translations.
- [ ] Closed-lid extraction continuation and green completion LED on actual hardware, bounded RAM use and interrupted-write handling.
- [ ] Future Update page remains an honest placeholder until GitHub updating is actually implemented and authorized.

Evidence: immutable candidate source/build/audit directories and per-attempt reports under`build/integrated-native/`; detailed chronology in`IN_GAME_OPTIONS_PROGRESS.md` and`PERFORMANCE_CAMPAIGN.md`. ROMs, saves, prepared copyrighted data and raw process captures are private and never part of a public package.

## NP49 renderer adaptation — 2026-10-19

- [x] Study MKW-3DS native rendering mechanisms and inventory 13 direct forks;
  distinguish Epic MK64 from the Wii codebase (see `NP49_RENDERER_STUDY.md`).
- [x] Add exact-value mesh uniform reuse and invalidate material state after
  highlight drawing in the generated integration; preserve pinned vendor.
- [x] Pass host equivalence/sanitizer gates, full build and package verification.
- [x] Verify candidate eb76eabaf6f4e95c includes all 47 current options source files.
- [x] Close Old castle-Yoshi and New Bob-Mario Azahar diagnostic sessions with
  three validated captures each and no detected emulator fault.
- [ ] Measure same-scene physical Old/New baseline-versus-candidate frame times.
- [ ] Complete broader UI, gameplay, stereo, sound and lifecycle acceptance.

Uniform work avoided in final cumulative emulator snapshots: Old 22.49%, New
20.34%. These percentages are not FPS improvements. Current result and limits
are documented in `NP49_RENDERER_STUDY.md`; this is a local candidate only.

## Seguimiento Old físico y estudio de otros ports — BUILD 54–55

- [x] Investigar código ARM11/PICA de SoH, SM64, OpenLara y G-Diffuser;
  registrar fuentes y límites en `3DS_PORT_RENDER_RESEARCH.md`.
- [x] Rehabilitar solicitudes de dump nativo (antes se descartaban), registrar
  intervalos y fallos de carga; captura completa local en Azahar BUILD 54.
- [x] Corregir brillo ON/OFF y enrutamiento MAIN LCD según POWCNT, con pruebas.
- [x] Compactar normales iluminadas: 15→12 filas, 27→21 productos vectoriales;
  53 pruebas y enlace ARM BUILD 55 pasan. No equivale a una ganancia física.
- [ ] Confirmar dump, estrella arriba, menú/cabeza y Adventure en Old físico.
- [ ] Reducir llamadas por pieza y los productores 2D pendientes.
- [ ] Bob-omb Battlefield: misma versión, Old/New secuenciales y aceptación física.

## Objetivo nuevo del propietario: rendimiento mínimo 2× sobre BUILD 55

- [x] Congelar referencia y criterio en `PERFORMANCE_DOUBLE_BUILD55.md`.
- [x] Separar sobrecoste de perfilado fino: alrededor de 0,8 ms en cabeza/Azahar.
- [x] Registrar referencia Bob55 y candidato61 Old/New/emulación con máscaras hexadecimales corregidas, diálogo cerrado y recorrido completo; evidencia `bob61-{old,new}-verified.json`.
- [x] Rechazar y revertir BUILD 56: menos filas pero sin mejora de tiempo total.
- [ ] Reducir el coste total al menos 50%; conservar picos e intervalos por fase.
- [ ] Verificar el resultado en Old/New reales, sin extrapolar desde Azahar.

- [x] Precargar cabeza202 al arranque: BUILD57 conserva imagen y elimina su carga al revelarse en Azahar. Validación física pendiente.

## Reducción sostenida posterior a precarga — BUILD58–61

- [x] Separar coste SDK CPU por muestreo rotatorio, sin confundirlo con GPU.
- [x] Probar buffers agrupados en506 bancos; A/B sin mejora temporal, quedan opt-in.
- [x] Eliminar envíos de marcadores degenerados; 22 menos en cabeza, imagen igual.
- [x] Comparar movimiento Bob Old/New: mejora pequeña de render (2–4%), aún con picos. No cumple2× ni estabilidad60.
- [ ] Validar movimiento Bob y aparición de cabeza en Old/New físicos.
- [ ] Reducir llamadas mediante atributos por instancia y preparación compartida de luces.
- [ ] Alcanzar el objetivo2× sobre55; aún no demostrado.

## Precarga ampliada y reutilización — BUILD62–63

- [x] Reutilizar luces por contenido en la captura:53 pruebas, cabeza sin diferencias de imagen salvoBUILD; models3,63→3,20 ms en Old emulada.
- [x] Extender bancos de imágenes sin reconstruir las texturas residentes; laboratorio GPU de presupuesto/rollback/identidad/reutilización/move aprobado.
- [x] Precargar también botones/retratos222/4101; pico de revelado de cabeza553→10 ms en Old emulada. Carga adelantada al inicio, no eliminada.
- [x] New emulada: revelado9,7 ms máximo/18,223 ms intervalo máximo; misma imagen de cabeza queOld.
- [x] Cerrar A/B de igual binario (sin diferencias de imagen) y regresión Bob63 Old/New secuenciales (3.000 actualizaciones por perfil, sin rechazos; persisten picos).
- [x] CIA63 empaquetada/verificada e instalada en Old Azahar: estrella/cabeza/selector visibles; QR LAN y descarga verificados.
- [ ] Prueba física CIA63; objetivo2× sostenido continúa abierto.

## Coste sostenido de emisión — BUILD64–67

- [x] Comparar emisión de uniformes contra Citro3D1.7.1:431.792 actualizaciones por modo, incluidos cambios/restauración de shader.
- [x] Paquete nativo de30 palabras:100.000 rangos coinciden con el SDK; contexto y caché de vértices conservados.
- [x] 56 pruebas host, tres pruebas de emisión con sanitizadores y siete capturas GPU idénticas a63.
- [x] Corregir sobrecoste del selector experimental66 antes del A/B final; baseline55 no se redefine.
- [x] Comparar67 con55 en cabeza y Bob Old/New secuenciales: render activo cabeza26–28% menos, Bob17–21% menos; persisten picos de hasta~61ms. Objetivo2× pendiente.
- [ ] Validar en consolas físicas; el objetivo2× completo sigue pendiente.
- [x] CIA67 empaquetada y verificada; instalada en Old Azahar: arranque, cabeza y Aventura→selector con fondo superior visibles. QR LAN generado; hardware pendiente.


## Captura de skin y siguiente reducción general — BUILD68–70

- [x] Reutilizar conversiones/inversas por captura sin guardar poses entre actores/cuadros: cabeza skin1,32→0,92ms; render dominante sin mejora respecto67.
- [x] Validar cabeza y Bob68 Old/New secuenciales; imagen igual salvoBUILD/FPS, sin rechazos. Persisten picos de carga.
- [x] SCENE5 cubre skin/mesh, toon/grey, highlight, capas solid, tone y rangos de vértices; roundtrip C++ conserva campos;58 pruebas host pasan.
- [x] Capturar cabeza69 yBob70 y contar agrupación adyacente por valores: geometría149→34 y102→33–34 envíos potenciales.
- [x] Implementar paleta/batching con precisión de shader comprobada y medir frente55; la versión aceptada82 usa geometría GPU residente y una sola subida dinámica.
- [ ] Validar en Old/New físicos y alcanzar reducción mínima50% sostenida; sigue sin demostrar.
- [x] CIA70 empaquetada, política verificada y arranque/selector observados en Old Azahar; QR/descarga LAN verificados. Validación física pendiente.


## Agrupamiento experimental — BUILD71–75

- [x] CPU color entero, paleta16 y propiedad por cuadro: precisión validada, pero72/73 regresan; no aceptados.
- [x] Sustituir CPU color por atributos normales/base y paleta8; extremos signed16, tablas/toon/gris y constantes entre DVLE comprobados.
- [x] Lab75b GPU120cuadros, tres capturas idénticas;61pruebas host pasan.
- [x] A/B75 de igual binario en cabeza Old:6,993→7,122ms; misma imagen. Mantener nuevo agrupamiento desactivado por defecto.
- [x] Reducir preparación evitando rehacer topología/copia inmutable; campaña81 cabeza/Bob Old/New completa.
- [ ] Demostrar50% menos coste total respecto55 y comprobar en hardware. CIA70 entregada permanece igual.


## Topología residente experimental — BUILD76–78

- [x] Medir agrupación/remapeo/emisión; separar reloj de diagnóstico del A/B.
- [x] Caché compacta acotada, propiedad/move/rollback/rangos parciales, skin secuencial.
- [x] Plantillas y bucles ARM de atributos;61pruebas host y labGPU120cuadros, dos poses y prefijos exactos.
- [x] Cabeza78 Old A/B de igual binario e imagen idéntica; Old/New y Bob3.000actualizaciones secuenciales comparados con55/68.
- [x] Rechazar activación por regresión Bob6–13%; mantener78 optativo y CIA70 igual.
- [x] Implementar residencia GPU y una sola subida dinámica según `NATIVE_RESIDENT_PALETTE_DESIGN.md`.
- [ ] >=50% menos coste activo respecto55 en cabeza yBob Old/New; comprobación física separada. Sigue pendiente.

## Geometría GPU residente — BUILD80–82

- [x] Sustituir stream intermedio60bytes por grupos rígidos GPU, paleta8 y una única subida skin/quads36bytes.
- [x] Mantener shaders/mallas originales para toon, tablas, UV y bases fuera de signed16; presupuesto/rollback/move/rangos comprobados.
- [x] 61pruebas host con sanitizadores y labGPU120cuadros: dos poses, imágenes exactas y504bytes dinámicos por cuadro, cero rígidos.
- [x] A/B81 mismo binario cabeza/Bob Old/New secuencial, sin rechazos y con escenas/capturas iguales. Contra55: cabeza39–40% menos render activo; Bob28–33% menos. Aún no2×.
- [x] BUILD82 predeterminada, CIA empaquetada e integridad ELF verificada; instalada en Old Azahar: estrella, cabeza, Adventure y selector con fondo superior visibles. Precarga202/222/4101 en serial1, timings habilitados.
- [ ] Validación física de CIA82; no usar las medidas emuladas como FPS reales de Old/New.
- [ ] Reducir picos de carga y trabajo sostenido restante, incluida la ruta2D de juego que aún usa el puente PPU. Mantener el objetivo original>=50% respecto55.


## Frontera dinámica tipada — BUILD83–85

- [x] Muestrear etapas en cabeza/Bob; identificar~1,6ms de preparación dinámica en cabeza y el coste de subida2D en juego.
- [x] Validar NormalVertex tipado sin releer índices identidad inmutables; preservar dominio UV, normales y exclusión de prefijos inválidos de la paleta.
- [x] CTZ/CLZ exacto para precisión UV e intervalos ordenados para evitar el mapa por vértice cuando no hay solapamientos.
- [x] 61pruebas host con sanitizadores; oráculos de vértices/UV/solapamientos y labGPU120cuadros, dos poses e imágenes exactas.
- [x] Cabeza/Bob85 Old/New secuenciales: escenas y pantallas idénticas salvo26píxeles BUILD. Contra55:43–45% menos render activo cabeza;29–35% Bob. Aún no50%.
- [ ] Resolver picos de carga (hasta~59ms emulados), seguir reduciendo el puente2D temporal y demostrar>=50% en ambos escenarios/perfiles.
- [ ] Validación física Old/New del nuevo hito; no equivale a los resultados de Azahar.
- [x] CIA85 verificada e instalada en Old Azahar: estrella, cabeza y selector; QR/descarga LAN byte/hash comprobados. Sólo una instancia, cerrada al terminar.

## 2026-10-30 — bloqueo Old físico, BUILD88 →91

- [x] Preservar el log real88 de la SD, sin cambiar ajustes ni guardados.
- [x] Reproducir la estrella ausente con capturas intermedias y restaurar la
  admisión de matrices afines identidad desde el productor semántico.
- [x] Cuota nativa30% y alternativa de audio core0; inyección de rechazo core1
  completa Bob con el audio asíncrono creado, sin rechazos de presentación.
- [x] Caché CPU de un banco validado<=512KiB, extensión sin releer archivo;
  laboratorio GPU90cuadros y61pruebas host91 pasan.
- [x] Empaquetar CIA91, verificar ELF/política y copiar con hash a SD:/cias.
- [ ] Confirmar en la Old física91 el paso Adventure→selector→partida y el
  rendimiento sostenido. Mantener abierto hasta recibir su prueba; si falla,
  leer la última etapa de Native image I/O en native-test.log.
- [ ] Completar transformaciones2D no identidad y los productores pendientes;
  no declarar100%nativo ni60FPS físicos estables por esta corrección.
