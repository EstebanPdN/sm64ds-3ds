# Render nativo: errores reportados y lista vigente

## Candidata107 — 31 de octubre, aceptación física pendiente

Old: tope30FPS sin interpolación. New: objetivo60FPS con supersampling800x240.
Se conserva la interfaz propia. El mapa afín pasa a PICA por defecto, con
fallback CPU; la capa frontal requiere75%menos tráfico, no75%menos coste total.
El reloj de juego no rebasa sus plazos cuando cae la presentación y ésta
consulta al worker sin esperarlo. Las pausas explícitas desplazan el reloj.

En Bob, las últimas ventanas de120imágenes de107 dan29.92Old /59.83New en
Azahar. P99:36.911msOld /20.128msNew; máximos37.965/20.285ms. Hay ventanas de
carga de25.48/47.78FPS y picos mayores: aún no es estabilidad30/60global ni
validación física. La simulación mantiene los3000eventos en50.145s. Con80ms
inyectados en el worker Old,1200eventos tardan20.058s, sin rebases/refusals.

Los recorridos anteriores105Old/106New completan Adventure y la introducción
sin quedarse en blanco en emulador.107 conserva el camino de105: se descartó
la consulta adicional de106 por empeorar Bob. Falta reproducir y cerrar en la
Old real el corte musical y el fallo de Adventure reportados en91. El renderer
conserva puentes PPU para productores no migrados y cargas síncronas; no se
declara completada la migración nativa ni alcanzado el2x pedido inicialmente.

Ver checklist vigente y `hardware-report91/profile107-validation.json`.


## Prioridad actual — velocidad independiente del render, 2026-10-30

El propietario pide específicamente que bajar los FPS no ralentice la lógica
ni la música, y que el trabajo2D no bloquee el bucle. Se localizaron dos
acoplamientos: `Clock::begin_cycle` desplazaba el plazo por la tardanza del
ciclo previo; `finish_interpolated_frame(false)` esperaba al worker pese a su
argumento. Además, `present_playable` preparaba2D antes de otra espera.

La ruta nativa usa ahora plazos fijos, un único job pendiente con sondeo sin
espera y retirada de las capturas no publicadas. Se cancela un endpoint
obsoleto antes de reutilizar su store. Las capturas diagnósticas y el lienzo
de la cabeza conservan su barrera explícita; no son trabajo de presentación
normal. El tiempo de un dump intencionado se excluye del reloj por una API
de pausa, incluso si falla el readback. En Old la lógica tiene prioridad sobre
el render de core0; la espera nativa deja de consumir1.5ms de giro activo.

Prueba interna98: demora artificial80ms dentro del worker,1.200 eventos DS y
actualizaciones,191 imágenes,1.009 publicaciones omitidas,0 rechazos,
0 rebases y0 ticks descartados. El tiempo del launcher incluye el arranque;
no se presenta como medida exacta de Hz de lógica ni como resultado físico.

La captura8FPS obliga a rechazar95. Restituir audio a core1 en96 devuelve
Bob-omb a un coste parecido a91, pero no elimina la cinemática lenta. La
atribución fina demuestra que ENGINE_B rasteriza fondos y sprites en CPU.
La ruta nueva publica el raster inmutable del mapa y un foreground propio;
PICA hace el muestreo afín y la ampliación sharp2x. Las mezclas OBJ soportadas
conservan el cálculo entero de seis bits sólo donde hay un sprite visible.
El renderer recibe datos tipados, sin registros ni comandos gráficos DS.

El diferencial host verifica3.145.728 píxeles contra el PPU completo; falta
verificar el resultado del muestreo real de PICA. El laboratorio ARM está
compilado. Otra prueba de Azahar de un proyecto distinto impide iniciarlo
sin incumplir la regla de una instancia. No se detiene esa prueba ajena.
No hay una CIA nueva aceptada o entregada. La validación física91 y la
mejora2x siguen abiertas. Lista vigente: `CURRENT_WORK_CHECKLIST.md`.

## Estado físico vigente — BUILD91, 2026-10-30

El propietario confirma la estrella arriba. Adventure sigue quedándose en
blanco y exige una transición sin pausas visibles, incluidas pausas de dos
segundos. Apagó antes de un minuto: el registro no distingue por sí solo una
espera larga de un bloqueo permanente. El menú ralentiza también el juego y
el audio; ese reporte permanece abierto.

Se preservaron el log y el dump completo (57 regiones, 90.722.304 bytes de
payload) en `build/native-renderer/hardware-report91/`. Los 3.239.176 bytes
ejecutables de la RAM coinciden exactamente con el ELF de la CIA91. Se leyó
la RAM con sus direcciones virtuales y los offsets DWARF de ese ELF; no se
inventaron registros de CPU ni se adjuntó un depurador a la consola.

Los 1.024 frames guardados muestran dos fases: frames480–719, 57,29FPS y
34,24 draws/frame; frames960–1266, 40,71FPS y85,97 draws/frame. En esta última
fase, intervalos de presentación media24,562ms, p95=33,377ms, p99=64,115ms y
máximo96,693ms. GPU anterior: media9,146ms, p95=9,754ms, máximo9,996ms. La
escena guardada contiene131 partes, 1.038 vértices de skin, 66 quads y9 capas
2D. Las mediciones update/worker son tiempo de pared con preempciones y
esperas; con timings=false no se suman ni se presentan como CPU activa.

El registro acaba DESPUÉS de abrir, leer, validar y subir imágenes4111, en
serial1413. La siguiente ruta carga un lienzo del selector sin trazas finas
en91. Ya no se atribuye el bloqueo a la lectura del banco4111 ni se declara
corregido por su caché. Los lienzos del selector ocupan967.168, 475.648 y
1.491.456 bytes de fuente; la transición no debe pagar estas cargas.

En la RAM el backend de audio está abierto, audio_ok=1, hay PCM no nulo y
tempoRate=256/256, con tasa32728. Contador publicado: 2.298 buffers enviados,
2.295 completados, cero vaciados completos observados. **Eso no descarta el
defecto audible**: el dump no es atómico para audio/DSP, y ese contador puede
omitir un vaciado que sucede durante el relleno de otro buffer. El tiempo de
pared del pump incluye su espera y la cuota de core1; no equivale a CPU activa.

También se encontró desactivada por defecto la recuperación de cadencia en
el render nativo. Una presentación lenta podía ralentizar la simulación.
La corrección debe retirar explícitamente la captura que no se publica,
conservar el otro store prestado al worker, proteger capturas diagnósticas
y capturas del lienzo de Mario, y evitar interpolar otro frame para recuperar
el que se acaba de omitir. Esto conserva la velocidad del juego bajo carga;
no demuestra60FPS ni una mejora2× de trabajo por frame.

Pendientes: validación física de la siguiente CIA, transición Adventure,
ritmo del juego/audio y menú sostenido; después Bob-omb Battlefield físico.
Análisis reproducible privado: `hardware-report91/analysis.json`,
`physical-audio-memory.json`, `elf-ram-identity.json` y `receipt.json`.

Actualizado el 2026-10-27 tras las dos capturas de diálogos. Esta lista reúne
los reportes del usuario de este hilo y sustituye las listas parciales del
render nativo. Registrar un error no significa haberlo corregido. Los reportes
sin verificación en la versión final permanecen abiertos.

## Objetivo y punto de partida

Recuperar la presentación y funcionalidad de BUILD 24 con el render nuevo,
corregir los fallos reportados y el rendimiento, y continuar la conversión
de todos los sistemas restantes a ejecución nativa de 3DS.

La evaluación anterior está en
`build/native-renderer/playable-r1/EVALUATION-2026-10-27.md`.
Existe una prueba de referencia p26 y una implementación p27 con worker y
etiqueta `BUILD 25 N27`; la revisión de p27 quedó interrumpida. No se da por
aceptada esa versión ni se extrapola su resultado al hardware. `STATUS.md`
conserva información histórica de p9–p20; no describe por sí solo p27.

## Seguimiento de errores

- [x] **BUG-18 — Letras deformadas con el diálogo nativo de BUILD 43.**
  Reporte del propietario, 2026-10-29: la frase «First, talk to the Bob-omb
  Buddy» presenta trazos irregulares. La prueba del atlas en cuadrícula no
  cubre avances variables ni todas las fases del escalado 5/4. Exigir una
  comparación de píxeles de frases y scroll y una revisión del diálogo real.
  BUILD 44 corrige la cobertura de los bordes: a escala 5/4, ciertos bordes
  caen sobre centros de píxel y la regla de relleno de PICA omite o añade una
  columna. Desplazamiento de 1/16 de píxel, con la misma política ya usada
  en los lienzos nativos. La prueba con posiciones variables y las cuatro
  fases de scroll pasa de 8.199 diferencias a cero en 1.536.000 píxeles por
  modelo emulado. La frase reportada y el avance con A se verificaron en
  Bob-omb Battlefield. Evidencia privada:
  `build/native-renderer/playable-r1/dialogue-build44-validation.json` y
  `sessions/dialogue-p44-old/screenshots/_29.10.26_12.40.24.689.png`.
  Aceptación física pendiente; no se creó una CIA.
  El menú principal también baja a 30 FPS o menos; continúa abierto bajo
  BUG-17 y necesita medición propia, no extrapolación de la prueba del atlas.

- [ ] **BUG-17 — Bajón al aparecer la cabeza de Mario y al hacer scroll de
  diálogo con A.** Reporte del propietario, 2026-10-29, tras confirmar mejora
  de la espera de presentación. Separar primera carga de modelos/texturas de
  la cabeza y el coste recurrente mientras avanza el texto negro al entrar a
  Bob-omb Battlefield. El puente 2D sigue activo y sus planos se invalidan
  con texto animado; es una hipótesis con coste medido, no una causa única
  demostrada. Comparar antes/durante/después del scroll y conservar CIA BUILD
  42 como hito de sincronización para la prueba física solicitada.

- [x] **BUG-16 — Clic del ratón en la pantalla inferior.** Verificado en la
  sesión local de Azahar con BUILD 35: clic en cabeza, lápiz y retorno;
  el HID llega y el juego responde. No se ha identificado una causa del
  fallo de la sesión anterior ni se ha cambiado el mapeo de entrada.


- [x] **BUG-15 — Cabeza dibujada y lienzo corruptos.** Al pulsar el botón
  de dibujo del menú en BUILD 34, el lienzo aparece blanco con franjas de
  píxeles de colores o papel vacío. Comprobar captura inicial de la cabeza,
  dibujo con el lápiz, uso del dibujo y retorno al menú. Reporte del usuario
  2026-10-29, adjuntos `72944e08-e7bc-4ada-bb36-8e400a2014bf.png` y
  `58afef5d-0786-4e40-848a-e5dc50af6366.png` del hilo.
  Reproducido en Azahar con BUILD 34. El toque del ratón llegó al juego
  (HID 290,212 → DS 232,169 al pulsar lápiz); el fallo del lienzo persiste
  después de recibirlo. Dos rutas ausentes verificadas en fuente: captura
  de engine A a bank D y productor de trazos `c19cc`. BUILD 35 implementa
  ambas; comprobadas visualmente entrada, lápiz, trazo continuo, mano y
  retorno al menú (botón inferior izquierdo). Validación física pendiente.


Las casillas marcadas indican comprobación local, con la evidencia al final.
La aceptación en la consola real se registra por separado.

- [x] **BUG-14 — Wide del título parpadea y desaparece.** Nuevo reporte del
  usuario durante la revisión de BUILD 32: aparece durante unos segundos y
  luego desaparece. La captura muestra franjas negras a los lados del título
  mientras el contador conserva su posición ancha. Verificar continuidad
  durante toda la animación de estrellas, entrada y salida del menú; una
  captura aislada con los laterales correctos no cierra este fallo.
  Evidencia: `93fc6b72-42f6-41d8-8898-3d5d23fb05b3-700f4a5b-f8ae-430d-9b7c-9138a5607559.png`
  en el directorio privado de adjuntos del hilo.

- [ ] **BUG-01 — Cuadros de diálogo ocupan toda la pantalla.** Reporte nuevo:
  cajas de texto desbordadas, oscurecimiento extendido y texto recortado o mal
  colocado durante las escenas de Lakitu/Yoshi. Comprobar dimensiones,
  composición, transparencia y recorte durante apertura, avance y cierre;
  al cerrar debe desaparecer todo residuo. Evidencia: las dos capturas al final.
- [x] **BUG-02 — Fondo blanco en el selector de archivos.** Al entrar por
  Aventura aparecen los tres archivos, pero falta el fondo que debería verse
  detrás. Recuperar el fondo y sus capas durante entrada, selección y salida.
- [ ] **BUG-03 — Menú de pausa corrupto.** El usuario reportó comportamientos
  gráficos extraños al pausar. Revisar apertura, navegación y reanudación,
  incluyendo restos de capas y posición/escala de cuadros y textos.
- [ ] **BUG-04 — Pantalla superior sin Wide como BUILD 24.** Restaurar el
  encuadre y la extensión del fondo/cielo; no ensanchar textos ni logotipos.
  Comparar título, menú y juego con la referencia antigua.
- [ ] **BUG-05 — Distorsión en los laterales del mapa/imagen superior.**
  Reporte independiente del ancho: revisar bordes, UV, recorte y transiciones
  del mapa superior; ocupar todo el ancho no basta para cerrar este error.
- [ ] **BUG-06 — Contador de FPS incorrecto.** El usuario rechazó la exactitud
  de las lecturas. Contrastar el valor mostrado con tiempos reales y explicar
  qué mide: actualizaciones del juego, cuadros presentados o VBlanks/VPS.
- [ ] **BUG-07 — Lectura clavada en 39,9 al subir la estrella.** Reproducir
  esa transición y distinguir un contador defectuoso de un límite real de
  presentación. No atribuir la causa a VBlank o interpolación sin evidencia.
- [ ] **BUG-08 — Bajones de rendimiento.** Caída reportada desde 60 a unos
  15 al aparecer menú/cabeza de Mario, seguida de unos 25–27; también hay
  bajones en juego. Medir por fase y eliminar trabajo redundante identificado.
- [ ] **BUG-09 — Sonido entrecortado con las caídas.** Verificar continuidad
  del audio durante menú, cargas, cinemáticas, juego y pausa, además del FPS.
- [ ] **BUG-10 — Bloqueo en pantalla negra.** Reportado durante iteraciones
  anteriores. Verificar avance real desde arranque hasta juego y reanudación;
  un proceso vivo o un contador que cambia no prueban progreso.
- [ ] **BUG-11 — 3D estereoscópico no funciona.** Recuperar ambos ojos y el
  control mediante slider; comprobar UI y transiciones, también en consola.
- [x] **BUG-12 — Número de build repetido.** Todas las versiones mostraban
  BUILD 24. p27 introdujo `BUILD 25 N27`; verificar numeración inequívoca en
  cada nueva compilación/entrega y coherencia entre pantalla, log y paquete.
- [x] **BUG-13 — Contadores del HUD inferior ausentes.** Observación del
  asistente durante p27, no un nuevo reporte atribuido al usuario: faltan vidas
  y estrellas. Recuperarlos y comprobar mapa, iconos y capas durante juego.

## Reportes anteriores que deben conservarse como regresiones

El usuario indicó después que personajes y patrón habían mejorado, y que la
animación de la estrella parecía resuelta. Eso no verifica la compilación
final. Mantener estos casos explícitos en la comprobación conjunta:

- [ ] **REG-01 — Mario del menú y sus pupilas.** Cara deformada, ojos blancos
  o sin pupilas; comprobar también movimiento de ojos y texturas animadas.
- [ ] **REG-02 — Peach en la cinemática.** Modelo/pelo/transparencias rotos.
- [ ] **REG-03 — Yoshi.** Ojos deformes, párpados ausentes y lengua estirada.
- [ ] **REG-04 — Mario y Luigi de la cinemática.** Ojos ausentes y problemas
  de articulaciones/gorra. Incluir Wario como control: el usuario no había
  observado problemas en él.
- [ ] **REG-05 — Patrón del fondo del menú/logotipo y fondo de la estrella.**
  Antes aparecía un patrón blanco incorrecto donde debía verse negro; el
  patrón debe verse correctamente en el menú cuando corresponde.
- [ ] **REG-06 — Animación inicial de la estrella.** Encogimiento al tocar y
  apertura de la transición negra, sin formas ni capas corruptas.
- [ ] **REG-07 — Render 3D ausente.** El primer bloqueo del hilo fue que no
  se veía ningún 3D; conservar arranque, menú y primer nivel como prueba básica.

## Orden de trabajo vigente (última instrucción del usuario)

**Actualización de prioridad, 2026-10-29:** el propietario pide completar
primero el 2D nativo y hacer el juego entero nativo, y prohíbe compilar otra
CIA antes de completar la migración. Esta condición sustituye la entrega de
CIA en hitos parciales de la lista histórica siguiente. El objetivo y todos
los fallos pendientes siguen activos. BUILD42 ya se había empaquetado antes
de recibir esta instrucción.

1. Corregir todos los bugs reportados y verificar juntos diálogos, selector,
   pausa, HUD, ancho/bordes y estabilidad. Los fallos que dependen de la nueva
   presentación también permanecen vinculados a los hitos siguientes.
2. Completar la arquitectura de presentación con worker de render en core 2.
3. Presentación a 60 FPS en New 3DS con cuadros interpolados, como BUILD 24.
4. Paridad visual con BUILD 24: Wide, estéreo, transiciones y regresiones.
5. Sustituir el puente PPU por 2D nativo, por sistema y con cobertura explícita.
6. Resolver huecos 3D y retirar las capas de traducción DS restantes. Un censo
   de registros a cero no prueba ausencia de accesos directos a VRAM/OAM/paleta.
7. Preparar una CIA para prueba física después de **cada hito**, con identidad
   de build inequívoca y evidencia de emulador separada de la aceptación física.

El paso 7 es una condición de entrega recurrente, no una única entrega al final.

## Evidencia nueva de BUILD 29 y BUILD 30

- BUILD 29: diálogo de Lakitu rectangular y legible observado en Azahar. La
  preparación de candidatos serializada empeoró tiempos; no aceptada como
  solución de rendimiento. Selector todavía blanco.
- BUILD 30, sesión `bugs-p30`, 300 s: 7560 cuadros, dos transiciones de escena,
  diálogo rectangular observado, cierre sin restos, pausa/reanudación y juego
  con HUD de vidas y estrellas. La ausencia del HUD en cinemática no demuestra
  BUG-13 durante juego; queda en la comprobación de regresión final.
- La pausa inferior se ve completa; arriba el oscurecimiento deja laterales
  claros. El selector tiene una malla animada pendiente de productor nativo:
  la traza identifica `func_ov007_020ca5f0` / `020caeac` como escritor descartado.
- Juego estable al final de BUILD 30: intervalos medios ~33,4 ms. Menú y
  cinemática todavía tienen caídas. No equivale a 60 FPS ni a prueba física.
- La numeración visible ya avanza BUILD 29 → BUILD 30; el empaquetado de CIA
  deberá conservar la misma identidad antes de cerrar BUG-12.
- Las capturas RGB internas del render presentan corrupción de lectura y no
  se usan como evidencia visual. Las observaciones anteriores son capturas
  reales de la ventana de Azahar; los planos CPU sirven solo para diagnóstico.
- En preparación BUILD 31: productor de malla dinámico nativo, iluminación
  GPU y highlight del selector; extensión del efecto de pausa basada en su
  máscara, sin extender textos. No se da por validado hasta ejecutar la build.
- BUILD 31, revisión visual del selector: el fondo continúa blanco aunque la
  malla se captura sin descartes. No aceptado. BUILD 32 corrige la composición
  de la transformación: `0x04000444` es MTX_PUSH, no un cambio a proyección.

## Criterio de trabajo y cierre

### Evidencia de BUILD 32 y BUILD 33

- BUILD 32: el selector ya muestra el reflejo animado del castillo, con los
  tres archivos y botones legibles. Captura real guardada en
  `build/native-renderer/playable-r1/sessions/selector-visual-p32/screenshots/_27.10.26_20.26.25.853.png`.
  Los dos pases de la malla tienen 828 de 1092 vértices dentro del volumen de
  cámara; no hay descartes de dibujo en el selector.
- BUILD 33: la extensión del título se obtiene de los fondos antes de los
  OBJ animados, y se copia a la captura propiedad de cada cuadro. No conserva
  referencias al caché de fondos mientras otro núcleo lo modifica. La traza
  de `bugs-p33-r2` mantiene `wide=1` durante todo el título. La CIA instalada
  también conserva Wide en capturas separadas por 62,890 segundos, durante
  distintas fases de las estrellas, sin repetir textos en los laterales.
- La misma build sube las dos publicaciones de interfaz dentro del cuadro
  GPU de la escena. La espera adicional de VBlank está desactivada: el reloj
  del juego sigue siendo responsable de su cadencia. Los contadores del
  productor viajan con cada trabajo y se leen antes de liberar la captura.
- `bugs-p33-r2`, 300 s: 7800 cuadros, dos transiciones de escena, sin bloqueo
  ni fallos de presentación. Se observó Peach y su carta, juego con HUD,
  movimiento, pausa completa y reanudación. Captura real de pausa:
  `build/native-renderer/playable-r1/sessions/bugs-p33-r2/screenshots/_27.10.26_20.35.35.328.png`.
  Hubo interacción manual al final; no es un benchmark limpio ni hardware.
- Tramo final del juego: ~33,43 ms entre presentaciones y ~6 ms de trabajo
  de presentación. El menú y las cargas conservan picos; no se declara 60 FPS.
  Hay modelos sin asociación y seis descartes acumulados: siguen pendientes
  en la cobertura 3D. La prueba no cierra esos huecos.
- Compilación ARM correcta, 45/45 pruebas host y 7/7 del preparador correctas.
  CIA privada creada en `build/native-renderer/playable-r1/hardware/build33/`;
  instalación, arranque, estrella, título y selector comprobados en Azahar
  (`hardware/build33/local-validation.json`). Prueba física pendiente.
- Worker solicitado en core 2, creación correcta. El valor `actualCore=0`
  de Azahar no verifica afinidad: la consulta SVC 0x11 no está implementada en
  [Azahar 2126.2](https://github.com/azahar-emu/azahar/blob/2126.2/src/core/hle/kernel/svc.cpp).
  El resultado de esa consulta debe verificarse en la consola.

La sesión `bugs-p33` se canceló durante el arranque por un guion de entradas
incorrecto; no se usa como evidencia. `bugs-p33-r2` reutiliza el guion numérico
completo de `bugs-p31`. La carpeta `hardware/build31` es una preparación
interrumpida, no una entrega.

Una sola instancia de Azahar. Recoger primero datos suficientes; aplicar un
cambio coherente y después hacer una prueba completa con capturas reales,
tiempos por fase, logs y diagnósticos pertinentes. La ruta incluye estrella,
menú/cabeza de Mario, Aventura, selector, cinemática, diálogos de Lakitu/Yoshi,
juego con movimiento, HUD y pausa/reanudación. Revisar audio y progreso real.
Comparar las pantallas afectadas con BUILD 24, sin tratarla como libre de fallos.

Para cerrar un BUG/REG, anotar build exacta, escenario, evidencia y resultado.
Las cifras visibles de 29,9 y 25,8 FPS en las capturas nuevas no bastan para
validar el contador ni el rendimiento sostenido.

## Capturas del reporte nuevo (privadas, fuera del repositorio)

- 19:32:55 — diálogo de Lakitu.
- 19:42:39 — escena de Yoshi y texto recortado.

## Continuación 2026-10-29 — BUILD 34

Worker nativo identificado ante el asignador para que sus reservas no entren
al heap del juego. Audio trasladado a su hilo de core 1; no se crean los
workers antiguos de render/PPU. El registro worker-p34 alcanza 91560 cuadros,
sin errores de presentación, con dos transiciones y tramo final de ~33,43 ms.
La prueba automática quedó interrumpida y dejó avanzar el emulador más de los
300 s previstos: no se considera observación visual continua. Al retomar,
el proceso ya había terminado.

CIA privada BUILD 34 creada con los bancos de esa sesión; instalación y
arranque verificados en Azahar, con título y versión visibles. Entrega:
`build/native-renderer/playable-r1/hardware/build34/SM64DS-Native-BUILD34.cia`.
Prueba física e interpolación pendientes. El FPS actual cuenta envíos
correctos al renderer por tiempo real; no cuenta barridos físicos del LCD.

## Hito BUILD 35 — cabeza dibujada (2026-10-29)

Se mantiene la animación, selección e input del juego. `c4fcc/c19cc` publican
color, puntos y progreso del trazo como quads nativos; ya no se pierden sus
escrituras GX. La transición captura una vez la geometría GPU, la compone
con los planos 2D antes del brillo maestro y escribe RGB555 en el lienzo de
bank D cuando pasa a BG3. El worker termina esa captura antes de permitir
el remapeo del siguiente update. El título/HUD nativos no entran en ella.

Evidencia local en `build/native-renderer/playable-r1/`:

- `sessions/head-p35/screenshots/`: Yoshi dibujado y lápiz; toque real del
  ratón registrado como HID, sin cambiar el mapeo táctil del juego.
- `sessions/head-p35-r2/screenshots/`: Mario dibujado limpio. El primer
  guion tocaba antes de acabar la entrada; no se usa como prueba del trazo.
- `sessions/head-p35-r3/input-script.txt`: guion con tiempos corregidos,
  sin recompilar. `screenshots/_29.10.26_06.02.45.214.png` muestra la línea
  continua, persistente al pasar a mano. El toque mueve el dibujo.
- `sessions/head-p35-r3/screenshots/_29.10.26_06.08.25.796.png`: retorno
  al menú 3D y sus botones tras pulsar el retrato inferior izquierdo.
- `sessions/head-p35-r3/user/sdmc/3ds/sm64ds/log.txt`: captura correcta,
  commit de bank D, productores válidos; sin rechazo ni geometría descartada
  en el recorrido. No es una prueba de rendimiento ni de interpolación.
- CTest: 46/46. Geometría de trazos además ejecutada con ASan/UBSan:
  puntos aislados, uniones, ángulos que cruzan ±pi, progreso parcial,
  saturación de extremos y capacidad insuficiente sin escritura parcial.
- `hardware/build35/SM64DS-Native-BUILD35.cia`: paquete local privado,
  título de prueba `000400000F536500`. Prueba física pendiente.

Este hito corrige el lienzo y los trazos; no completa los niveles a 60 FPS,
la paridad total con BUILD 24 ni la sustitución completa del puente PPU.

CIA BUILD 35 instalada y arrancada en Azahar 2126.2: cabeza de Mario y
retorno al menú 3D verificados. En la ruta que lo exige, mantener pulsado
el retrato inferior izquierdo más de un segundo. Evidencia estable en
`hardware/build35/validation/`; detalle en `local-validation.json`.

## Reporte físico Old — BUILD88, 2026-10-30

El propietario confirma pantalla superior negra tras tocar la estrella,
menú que cae de60 a30 y Adventure detenido en blanco con audio cortado.
Registro físico copiado intacto de `3ds/sm64ds/native-test.log` a
`build/native-renderer/hardware-report88/old-native-test.log`; hash/alcance en
`receipt.json`. Esto invalida cualquier interpretación de las corridas Azahar
anteriores como aceptación física o como prueba del recorrido completo.

Evidencia física: APT_SetAppCpuTimeLimit devuelve `d8e05bf4`; el worker de audio
core1 no se crea. En cabeza la ventana840 marca30,34presentaciones/s y un
intervalo máximo109,576ms; en960,29,93/s y78,045ms. GPU previa~7,8–8,1ms,
frente a~0,68ms en emulador. Los ámbitos de pared están anidados y no se suman.
El último mensaje es el comienzo de carga del banco4111 en serial974; no prueba
por sí solo si se bloqueó fopen, lectura, validación o subida. No se atribuye
el bloqueo físico completo a una fase sin una traza más precisa.

### Correcciones91 y aceptación pendiente

- La ruta nativa limita a30% su solicitud de tiempo en core1 (el exheader usa
  MaxCpu0x9E) y cae a un worker de audio core0 si core1 no puede crearse. No
  vuelve a audio síncrono sólo por ese fallo. Una prueba aislada fuerza la
  negativa real (`d8e05bf4` y threadCreate core1 nulo): core0 se crea y completa
  Bob3.000actualizaciones,2.997presentaciones,0rechazos. Es emulación con fallo
  inyectado, no una medida de audio físico.
- Se conserva un único banco de imágenes fuente validado, máximo512KiB, aparte
  de GPU LRU. Las extensiones de imágenes del mismo banco reutilizan bytes
  propios en vez de abrir, releer y verificar el archivo completo en cada
  nueva imagen. Se libera al reemplazarlo o apagar el renderer. No se suben
  imágenes no solicitadas. El laboratorio GPU retira su archivo sintético tras
  el primer cuadro, añade imágenes y completa90cuadros con una sola lectura;
  reset/reinicio de dueño y liberación pasan. No se afirma que esto solo
  resuelva el bloqueo de la primera lectura física.
- La captura2D rechazaba `affine=15` incluso con matriz identidad. Se verificaron
  los productores originales be8b8/be3bc: dieciséis bases Fix12, reducidas a
  signed8.8 mediante shift4/signed16. SUB puede usar la ruta ordinaria sólo si
  esa base semántica es exactamente identidad y no hay flip global. No se lee
  OAM emulada ni se identifica una estrella por ID para dibujarla aparte.
  Escalas/rotaciones siguen pendientes. Con el mismo toque220 y captura480,
  BUILD88 muestra negro y91 muestra estrella y destellos arriba. Se conservan
  capturas de300/360/480 para mostrar la entrada desde el borde, sin elegir
  sólo una captura tardía del título.
- La CIA desactiva el perfil fino del renderer nativo y activa trazas acotadas
  de etapas de carga (begin/opened/allocated/read/validated/uploaded/cached).
  Los diagnósticos de dumps elegidos por el usuario conservan su configuración.
  Retirar las sondas no se cuenta como mejora2×.

61/61pruebas host ASan/UBSan91 y enlace ARM pasan. Las pruebas de identidad
cubren truncamiento Fix12, términos negativos, escala, rotación y reducción
signed16. El laboratorio de imágenes pasó; su primer intento tenía una
expectativa antigua de tamaño del stream y se explicitó la ruta no agrupada
que esa prueba estaba diseñada para medir. No se descartó un error del juego
para hacer pasar el test. Campaña90 de cuatro corridas Old/New secuenciales:
imágenes iguales a88 salvo BUILD,0rechazos; sus picos de carga siguen abiertos.
91 incorpora además la estrella, por lo que esa diferencia visual es esperada.

Pendiente: comprobar91 en la Old del propietario, especialmente Adventure y
el coste sostenido. No se da por resuelto el bloqueo físico ni60FPS estables
por disponer de una CIA nueva. No se modifica ningún guardado del propietario.
