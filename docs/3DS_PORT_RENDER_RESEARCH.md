# Investigación de render nativo 3DS — 2026-10-29

## Evidencia y alcance

Petición: resolver la caída de la cabeza de Mario en Old mediante reducción
real del trabajo; estudiar otros ports y el uso de ensamblador ARM11.
La referencia física del propietario es ~20 FPS en menú y hasta ~7 con la
cabeza; Adventure queda en el fade blanco. Ninguno de esos fallos se da por
resuelto por alcanzar 60 en Azahar. No existe un factor fijo emulador/consola.

Se inventariaron seis repositorios públicos y se leyeron implementaciones
concretas de cuatro. Es una selección de arquitecturas pertinentes, no un
catálogo exhaustivo de todos los ports. Fuentes descargadas, commits y URLs:
`build/native-renderer/port-research-20261029/sources.json`. No se subieron
activos del juego ni datos privados. No se importó código ajeno.

## Técnicas verificadas en código público

- **SoH, 999sian**, commit `0b93bad4242e1d532dabf78241c685546b16483f`:
  [matrices ARM11/VFP](https://github.com/999sian/soh-3ds/blob/0b93bad4242e1d532dabf78241c685546b16483f/src/compat3ds/arm11/skin_matrix_mult.S)
  conserva el orden de cálculo y ABI, carga la matriz izquierda completa y
  reutiliza registros. ARMv6K/VFP, sin NEON. Su
  [backend PICA](https://github.com/999sian/soh-3ds/blob/0b93bad4242e1d532dabf78241c685546b16483f/platform/3ds/source/gfx_citro3d.cpp)
  mantiene estado, escribe texturas directamente en Morton y vacía rangos
  concretos de vértices. No usar su objetivo New como prueba de 60 FPS en Old.
- **SM64, mkst**, commit `3f3c01eae67d6e9f3af89bc8073e37a5d153e00f`:
  [audio](https://github.com/mkst/sm64-port/blob/3f3c01eae67d6e9f3af89bc8073e37a5d153e00f/src/pc/audio/audio_3ds.c)
  usa buffers NDSP con estados QUEUED/PLAYING/FREE/DONE y cuenta muestras
  pendientes; el hilo se coordina con el render. Esto respalda separar audio,
  pero no permite asignar libremente dos workers a core 1 de Old.
- **OpenLara**, commit `3b268ca4bf986c380f44a26ffd9218bf6d376249`:
  [shader de personajes](https://github.com/XProger/OpenLara/blob/3b268ca4bf986c380f44a26ffd9218bf6d376249/src/shaders/pica/compose_entity.v.pica)
  guarda 32 bases de dos vectores (cuaternión + traslación) y el vértice
  selecciona su hueso. Reduce cambios de estado entre piezas. No es una
  sustitución directa de nuestras matrices enteras: escala, iluminación y
  redondeo deben conservarse. El audio usa NDSP y un hilo de relleno.
- **G-Diffuser/F-Zero X**, commit `111a14c568324b8e998452214df3ce24284cf2ae`:
  [guía de hardware](https://github.com/cruxxxxxx/gdx-3ds/blob/111a14c568324b8e998452214df3ce24284cf2ae/docs/3DS-HARDWARE.md)
  distingue perfiles Old/New y rutas nativas de pista, máquinas, HUD y efectos.
  Old usa menor detalle de rivales. Su estrategia no justifica reducir nuestra
  fidelidad sin autorización; sí refuerza medir el presupuesto por modelo.

También se inventariaron 2ship2harkinian-3DS y DaedalusX64-3DS. Este último es
un emulador con recompilación dinámica, no un port nativo del juego, y no se
utiliza como demostración del coste de nuestra lógica.

## Aplicación a este proyecto

El render PICA nativo todavía tiene coste de adaptación: uniformes por pieza,
precisión entera DS, captura de poses y productores 2D pendientes. Compilar la
lógica para ARM11 no elimina esos costes. El ensamblador de matrices ya existe
(BUILD 41) y no resolvió el escalón de 39,9 por sí solo. Se mantiene; no se
reescribe toda la lógica a ASM sin atribuir antes el tiempo.

La captura Old de Azahar BUILD 54 con cabeza muestra unas 242 llamadas de
 dibujo, 227 matrices y 2.261 filas de iluminación por imagen. La memoria de
recursos GPU contabilizada es ~1,78 MB lineal y 1 MB VRAM, con 12 cargas y
ninguna expulsión/fallo en esa captura. No demuestra ausencia de presión en
el heap CPU ni descarta cargas frías como causa del trancón.

### BUILD 55: normales exactas más compactas

Se sustituye la descomposición base 32 de cinco dígitos por base 64 de cuatro.
Sólo bits 0..22 del coeficiente afectan a `signed11(sum >> 12)`. El normal
firmado de 10 bits se divide en bajo de 6 bits y alto firmado de 4. Dos etapas
propagan el acarreo y descartan los 12 bits fraccionarios; las siguientes
conservan 11 bits. Los productos/acarreos quedan dentro de la precisión entera
exacta de f24. La luz, UV, colores, lógica y cadencia no cambian.

Resultado estructural: 15→12 filas por base normal (−20%), 27→21 `dp3` en su
transformación (−22,2%). No es un porcentaje de FPS ni de todo el frame. Se
aplica a todos los modelos iluminados, sin condiciones por nivel o personaje.

Verificación: 53/53 pruebas host, incluido shader real interpretado con f24 y
comparación de 4.096 normales contra aritmética entera ancha antes de saturar
el color. Se conservan las pruebas de difusión, especular, shininess, toon,
UV y caché. ARM y contrato ABI enlazado pasan en BUILD 55.

### Trabajo que sigue abierto

1. Medir en consola cuánto ocupan lógica, captura/skin, envíos y GPU; validar
   los dumps rehabilitados y recoger el último banco antes del fade blanco.
2. Agrupar piezas compatibles mediante paletas de transformaciones y caché de
   geometría, preservando orden/transparencia/identidad y precisión. Ésta es
   la reducción de llamadas más prometedora observada en OpenLara, pendiente.
3. Suprimir reconstrucciones 2D y capas DS restantes en productores semánticos.
4. Evaluar ASM adicional sólo en kernels dominantes, con equivalencia exacta
   y benchmark de ciclos en ARM11. Menos instrucciones no asegura menos ciclos.
5. Comparar Bob-omb Battlefield en Old/New secuencialmente, con picos e
   intervalos, y repetir en hardware. El menú sirve para aislar la regresión,
   no reemplaza esta aceptación.

No se declara el juego 100% nativo ni estable a 60 FPS. La pantalla blanca de
Adventure y la caída física de Old siguen pendientes de validación y arreglo.

## Comprobación local de BUILD 55

El menú ejecutó 900 actualizaciones y cerró con código 0, sin rechazos de
presentación. En la ventana de cabeza que termina en frame 840, las mismas
242 llamadas / 227 matrices pasan de 2.261 a 1.814 filas de iluminación
(−19,77% del total de filas; las otras clases de uniformes no cambian).

La captura RGB de frame 650 incluye la cabeza en la pantalla inferior. Frente
a BUILD 54, las diferencias se limitan al contador FPS superior (96 píxeles)
y el número BUILD inferior (32). No se observa diferencia en cabeza, fondo,
texto ni geometría en esa captura. No sustituye la regresión de todo el juego.

Ambas ejecuciones de menú mantienen 59,86 llamadas/s en la ventana observada.
Intervalos P95/P99/máximo: BUILD 54, 16,946/18,077/18,091 ms; BUILD 55,
16,936/18,059/18,298 ms. El coste activo medio pasa de 10,148 a 9,941 ms,
pero una sola ejecución por candidato en emulación no atribuye una mejora
física ni demuestra estabilidad. No hubo compilación simultánea ni otro
Azahar. La evidencia está en `normal-radix-validation.json` y en las sesiones
`full-native-p54-old` / `normal-radix-p55-old`.

Las sondas de entrada fría de Bob (`normal-bob-p54-old` y
`normal-bob-p55-old`, 900 actualizaciones, mismo guion) terminan correctamente,
pero NO son una prueba de rendimiento sostenido: sólo la última ventana
alcanza scene=2 y mezcla cargas e interpolación. Picos de intervalo
299,851/306,036 ms, P95 59,714/62,391 ms y P99 93,678/100,153 ms para
54/55 respectivamente. No hay mejora global demostrada en esta sonda; la
compactación no resuelve esos picos. El coste 2D máximo sigue cerca de
16,3 ms. No se promedian esos datos con los del menú ni se atribuye la
variación de una corrida al shader. Es necesario ampliar y atribuir esos
picos en la siguiente etapa. Detalles: `bob-cold-probes.json`.

La sonda secuencial New BUILD 55 también termina con código 0. La ventana
fría de Bob alcanza intervalo máximo 301,747 ms, P95 60,535 ms y P99
93,707 ms. Sigue siendo emulación, con sólo una ventana mezclada con cargas;
no representa capacidad física New ni Old. Los tres procesos se cerraron
antes de iniciar el siguiente y no se dejó ningún Azahar ejecutándose.

Artefacto local 3DSX BUILD 55: SHA-256
`2f6d1122ab4dcf72a20956ff1b6cf4a7030bd89f3aab6312f2f779f42186616d`.
No se empaquetó ni distribuyó una CIA nueva en esta etapa.

## Revisión de arquitectura y fuentes — 2026-10-30 (base BUILD85)

La ruta 3D usa bancos nativos persistentes, shader PICA, paletas de ocho MVP y
bases normales por rango; no necesita interpretar comandos GX de DS para
esos modelos. Esto no completa la migración del juego: `SceneRenderer::draw_frame`
todavía sube `bridge_.back/front/other/sky` y `compose_bridge` los compone.
Se debe distinguir ejecución ARM11 nativa de lógica y ausencia de adaptación
de periféricos DS. Tampoco confundir el DSP de audio con la GPU PICA200.

### Qué aporta la investigación y qué ya está aplicado

- [Citro3D1.7.1 DrawElements](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/drawElements.c)
  actualiza contexto, emite el rango indexado y limpia la caché post-vértice
  en cada dibujo. Reducir llamadas tiene valor CPU y GPU, pero construir un
  stream completo por cuadro puede costar más. Nuestro histórico72–78 ya
  descartó variantes más lentas. La geometría residente81 evita esas copias.
- [Citro3D renderqueue](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/renderqueue.c)
  distingue la barrera de cola, sincronización de pantalla y vaciado de caché.
  El backend usa `C3D_FrameEnd(GX_CMDLIST_FLUSH)` con flush explícito de cada
  recurso modificado. Quitar flushes de datos dinámicos no es una optimización
  válida; los estáticos se suben una vez. Deben mantenerse fence y propiedad
  de recursos antes de escribir o liberar memoria GPU-visible.
- [OpenLara: compose_entity](https://github.com/XProger/OpenLara/blob/3b268ca4bf986c380f44a26ffd9218bf6d376249/src/shaders/pica/compose_entity.v.pica)
  selecciona bases residentes mediante un atributo del vértice. La idea ya
  está incorporada aquí con matrices completas, conservando redondeo/escala
  del juego. Copiar su representación por cuaterniones no preservaría por sí
  sola la semántica de las matrices Fix12 originales.
- [SM64: backend Citro3D](https://github.com/mkst/sm64-port/blob/3f3c01eae67d6e9f3af89bc8073e37a5d153e00f/src/pc/gfx/gfx_citro3d.c)
  mantiene buffers y cachés de estado de shader/texturas. La aplicación aquí
  es conservar residencia y minimizar estado sucio; sus FPS no predicen el
  coste de la iluminación exacta y de los productores de SM64DS.
- [libctru: threadCreate](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/thread.h)
  documenta un solo hilo de aplicación en core1 y el permiso para core2 en
  New. En Old, core1 está destinado al audio: añadir otro worker de render
  no proporciona automáticamente otro núcleo disponible.

### Prioridades concretas, sin cambiar fidelidad

1. **Preparación dinámica**: el perfil83–84 atribuye ~1,1ms a dinámica de la
   cabeza y ~0,3–0,6ms a Bob en muestras con sondas. Examinar ensamblado real
   antes de escribir ASM. BUILD85 compila Release `-O2`, ARMv6K/mpcore/VFP;
   subir flags a ciegas o usar instrucciones NEON no resuelve este trabajo.
2. **Plan y envío**: ~1ms de plan y ~1,9–2ms de geometría en muestras de cabeza.
   Evitar copias/búsquedas por pieza y validaciones repetidas con una frontera
   tipada; conservar rechazos, handles generacionales, orden y transparencias.
   No sumar estos tiempos de muestras instrumentadas a medias de otra corrida.
3. **Productores2D**: reemplazar la rasterización/copia del puente por quads,
   fuentes/atlas y planos nativos semánticos. El salto observado en preflight
   de Bob incluye la subida de esos planos. `source2D` también puede incluir
   espera/preempción: no atribuir todo su tiempo a un raster CPU sin separar.
4. **Cargas**: Bob85 aún alcanza intervalos ~59ms Old. Precarga por demanda
   previsible, recursos compactos y lectura secuencial requieren presupuesto
   y pruebas de expulsión. Una reducción del render sostenido no arregla
   automáticamente los picos de SD/subida. Mantener esas muestras en el informe.
5. **GPU física**: medir PICA y bus en consola antes de aumentar el ancho del
   vértice, trasladar iluminación a CPU o reorganizar texturas/VRAM. El tiempo
   GPU de Azahar depende del host, no mide ciclos del PICA200 físico.

La aceptación2× exige coste activo <=50% del mismo punto de partida en menú
**y** Bob, además de distribución de intervalos y picos. No se obtiene
simplemente al alcanzar el límite60 de pantalla. Los datos55→85 existentes
son emulación; la caída observada por el propietario en Old sigue necesitando
medición de ese ejecutable y esa consola para atribuir su causa física.


### Experimento medido86: selectores de matriz empaquetados

El ensamblado85 de `update_skin_stream` repetía cuatro veces carga de selector,
suma, incremento, comparación y escritura de byte por vértice.86 calcula la
palabra little-endian `0x03020100 + slot*0x04040404` y usa `memcpy` de4bytes,
que devkitARM16.1.0 baja a un `str` alineado. No usa type-punning ni instrucciones
posteriores a ARMv6K. Se aplica también al stream de quads. Stride, shader,
flush, indices y rechazo de selectores>=8 conservan sus contratos.

61/61pruebas ASan/UBSan y enlace ARM pasan. Cuatro corridas secuenciales con
los guiones85: cabeza1.200actualizaciones y Bob3.000, Old y New, cero errores
de timeline. Cabeza coincide salvo los píxeles de BUILD; ambas pantallas de
Bob coinciden exactamente. Cabeza Old5,507→5,354ms(2,8%) y New5,179→5,087ms
(1,8%). Bob Old5,560/6,184/5,300/4,138ms adelante/derecha/salto/atrás; la mejora
frente85 es pequeña (0,2–1,2%) y no se separa de variación de una sola corrida.
New no mejora todas las ventanas. Los intervalos máximos de carga llegan a
62,688ms Old y62,473ms New: no soluciona tirones ni demuestra2×. Datos completos:
`build/native-renderer/port-research-20261029/packed86-*-vs85.json` y
`packed86-validation.json`. Son medidas de emulador, no hardware físico.

## sm64ds-enhanced, revisión fijada del 31 de octubre de 2026

Repositorio clonado en `build/research/sm64ds-enhanced`, commit
`9d52d9998c0b4c9cb876536f040e4d0f80351932` (MIT). Se reconstruyó además
su upstream `bf42a2418d4396bdd8bb2d61a2cff6cf85c2b743` con los 79 parches
ordenados, en `build/research/sm64ds-enhanced-upstream`. No se modificó el
proveedor fijado del juego propio. La comparación incluye el código de
entrada, GPU, publicación, audio, memoria, archivos y guardado; los números
anotados en comentarios del otro port no son mediciones de nuestro juego.

Fuentes de implementación, fijadas al commit revisado:

- [Entrada y dry ticks](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/game/main_game.cpp):
  Old usa interpolación desactivada por defecto y New activada. Sus dry ticks
  conservan lógica y sonido y omiten generación gráfica; no funcionan con su
  interpolación activada. Reinicia el reloj tras cuatro períodos de retraso:
  no copiar ese reinicio en cada sobrecarga, que volvería a ralentizar el juego.
- [2D asíncrono](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/game/sub_async.cpp):
  Old core0, prioridad inferior a lógica, publicación sin esperar. El propio
  archivo advierte que comparte VRAM/OAM/paleta mutables y puede mezclar datos
  de ticks diferentes. Reduce el mapa a aproximadamente15Hz bajo carga. No
  trasladar esa carrera ni esa reducción permanente a nuestra interfaz.
- [Publicación y fences](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/src/card3ds.cpp):
  separa copia de entradas y terminación del trabajo, pero todavía espera
  `card3ds_wait_idle` al publicar. Nuestro camino normal mantiene una sola
  tarea en vuelo, consulta terminación y descarta la captura no publicada.
- [PICA, antialiasing y transferencias](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/src/gpu3ds.cpp):
  supersampling800x240, generación de imagen para evitar transferencias,
  registros compactos20bytes, índices16bits, bloques lineales alternos y
  paletas de20matrices. Esas últimas pertenecen a su intérprete GX y shader;
  no sustituir nuestras paletas nativas de8matrices ni iluminación Fix12 sin
  otra equivalencia. Su cola GPU adelantada intercepta estado privado de
  Citro3D; no es una optimización portable para insertar sin validar ownership.
- [Audio](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/game/out_3ds.cpp):
  mezcla por tramos64muestras y cede entre tramos a órdenes del juego. Nuestro
  backend ya tiene intención de espera; se reduce su tramo128→64, sin cambiar
  frecuencia nativa, volumen ni longitud de buffers. Sus parches de SWAV
  perezoso actúan sobre otro mezclador; el nuestro ya prepara ondas fuera del
  bloqueo antes de IPCSend. No se pueden aplicar literalmente al backend
  `sound_native` ni justificar cambiar32728.498 por32768Hz.
- [Paquete de archivos](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/game/files_pack.cpp):
  índice permanente y lecturas agrupadas, con archivos grandes en streaming.
  Nuestro `prepared.sm64ds` y HAL ya evitan extracción/aperturas por archivo,
  tienen caché/prefetch y presupuesto por modelo. No añadir otro paquete en
  paralelo. El parche de extracción70 acelera preparación, no FPS del juego.
- [Guardado coalescido](https://github.com/ethanseagull/sm64ds-enhanced/blob/9d52d9998c0b4c9cb876536f040e4d0f80351932/port3ds/game/save_async.cpp):
  agrupa EEPROM8KiB en un hilo y drena al suspender/salir. Nuestro reemplazo
  recuperable ya está probado; trasladar escritura diferida requeriría una
  migración independiente de durabilidad y shutdown, no ayuda al menú que no
  escribe guardado. No alterar guardados como parte de esta candidata.

La válvula de memoria del otro port drena el renderer y recorta texturas/ondas
bajo umbral. Puede introducir tirones; su Old solicita64MiB, nuestro CIA80MiB.
`appFree=0` por reserva newlib no prueba falta de heap. Sus límites de planos
2D cambian memoria por más muestreo CPU, desfavorable para nuestro cuello.
`MultiCopy32Bytes` usa memcpy sólo sin solapamiento: el solapamiento debe
preservar copia ascendente, no sustituir indiscriminadamente por memmove.
No existe NEON en ARM11; SIMDe no crea SIMD nativo en Old3DS.

### Integración propia BUILD104–105

Old: tope de una presentación cada dos eventos originales de pantalla,
interpolación desactivada. New: interpolación habilitada y geometría800x240
reducida linealmente a400x240. UI y pantallas2D mantienen su política anterior.
El reloj lógico conserva deadlines absolutos y no se rebasa por render lento.
HOME/sleep desplazan el reloj sólo por la pausa explícita, con fences antes
de que Citro3D desactive VBlank. Los dumps excluyen asimismo su pausa.

El mapa afín nativo se activa por defecto después de equivalencia exacta en
PICA:60transformaciones,4.608.000píxeles LCD coincidentes. Se conserva fallback
CPU para ventanas/efectos no admitidos y después de un fallo GPU. La capa
frontal256x192 se transfiere a256x256, no al antiguo duplicado512x512:
196.608bytes actualizados en lugar de786.432 (75% menos); el escalado sharp
ocurre en GPU. Esto reduce trabajo y tráfico de esa capa, no demuestra75%
menos coste total ni garantiza30FPS físicos. Las capturas son inmutables y el
hilo principal nunca reutiliza la captura que el worker está leyendo.

Pruebas host tras104:63/63 ASan/UBSan. Mediciones integradas y límites físicos
se registran en el checklist y recibos de la candidata finalmente entregada.

### Candidata de entrega107 y experimento descartado106

106 añadió una consulta del endpoint tras el plazo del reloj: recuperaba
endpoints en la introducción, pero empeoraba las ventanas de Bob.107 elimina
esa consulta y conserva la política de105. No se presenta el experimento como
una mejora.107 Old/New se ejecutaron secuencialmente sin perfil fino ni
capturas automáticas: ambos completan3000eventos con50145.356ms de reloj
nativo frente a50145.339ms previstos. El estrés80ms de106 usa el camino Old,
que permanece igual en107:1200eventos/updates,20058.153ms,207presentaciones,
ningún rebase ni rechazo. Son tiempos del reloj emulado, no hardware físico.

Últimas ventanas estables de120imágenes en Bob107: Old29.92FPS, intervalos
p95/p99/máximo35.768/36.911/37.965ms; New59.83FPS con AA,19.532/20.128/20.285ms.
Incluyendo cargas, las ventanas bajan a25.48Old/47.78New y presentan picos
mayores. No equivalen a30/60constantes globales ni prueban una mejora2x total.
La introducción todavía prepara otros planos2D en CPU y conserva cargas
síncronas. Evidencia completa, ventanas y hashes en
`build/native-renderer/hardware-report91/profile107-validation.json`.

La CIA107 incluye los bancos nativos en RomFS, conserva el guardado separado
native-test y la política Old80MiB/New124MiB. El QR es entrega privada en LAN,
no publicación de recursos. El propietario necesita mantener sus datos del
juego ya preparados en SD. La prueba física sigue pendiente.

## sm64ds-enhanced 1.1.0, segunda revisión — 2026-11-07 (base BUILD113)

Historial reescrito: 9 commits, etiquetas `v1.0.0`/`v1.1.0`, HEAD
`2f03ddd83ba9be25907991584fed794d2906c434`. El commit fijado `9d52d999`
sigue accesible; entre ambos sólo cambian README, docs, instaladores y
herramientas de CIA. `port3ds/src`, `port3ds/game`, `shaders` y los79 parches
son idénticos: el código de ejecución es el ya revisado el31 de octubre.

Pendiente de aplicar, comprobado contra nuestra base:

- Raíz cuadrada exacta por VFP (parche0018). Nuestro `isqrt64` en
  `port/ntr/io.cpp` es el mismo bucle de32 pasos en64 bits. Lo usan
  `NormalizeVec3`, `LenVec3` y `cstd::sqrt`. Su perfil: ~4% de un cuadro de Bob.
- División exacta por VFP (0037/0052/0063). Nuestro `run_divide` ya calcula
  una vez al leer y tiene camino32/32, pero los casos de64 bits siguen en libgcc
  (ARM11 no tiene instrucción de división). `NormalizeVec3` divide2^56/sq
  (modo64/64, fuera del límite2^53 de su `div64_vfp`). `cstd::fdiv` en
  `port/hal/cstd_div.c` es `(a<<12)/b` en C: la misma división lenta, sin MMIO.
- Fotogramas secos (DryPace, 0060/0062): ellos deciden antes de la
  actualización. Nosotros saltamos después (`observed_cadence::behind()` y el
  tope Old de `presentation_divisor`), cuando productores y skin ya corrieron
  (2,55+0,29ms en Azahar, Corriendo113). Nuestro `Render` nativo reemplaza el
  original, así que un `produce()` vacío no cambia la lógica del juego.
- Teselado por GPU con display transfer: su teselado CPU (igual a nuestro
  `tile_frame_rows_1x`) costaba ~4ms/cuadro en consola y ~3,5ms en la
  selección de archivo. Nuestra ruta sharp escribe4 palabras por píxel. Ellos
  también descartaron hashes de imagen completa por lentos en consola; nuestro
  delta compara bandas con `memcmp` contra una copia de196KiB.

Ya cubierto: no usar SYNCDRAW (retraso de4–6ms de GPU), esperar
`gspIsPresentPending`, stdout con búfer, paquete de archivos (abrir y cerrar en
SD cuesta30–40ms), mapa afín en GPU, BG de texto en caché, HBlank una vez por
cuadro y ninguna espera del worker durante el fundido de salida. No aplica: su
registro GX, list memo e interpolación sobre GX; nuestro modo nativo descarta
el FIFO GX. Referencia de consola: su Bob en Old es ~21ms de tick más ~12ms de
present, ~24FPS sin ticks secos.

### Integración propia BUILD114

Aplicado de la segunda revisión: división y raíz cuadrada exactas por VFP
(`ds_math_unit.h`, también en `integer_sqrt` y `cstd::fdiv`), actualizaciones
secas decididas antes de dibujar y LZ77 sin comprobación por byte. Además, los
planos 2D superiores se suben a 1x y la GPU hace la copia 2x en VRAM, como el
mapa (citro3d sólo admite render targets en VRAM; si no queda sitio, el plano
conserva la ruta CPU 2x). Azahar Old: 29,6FPS de media en los 50 niveles con
el tope de30; New: 59,6FPS con AA. Son tiempos de emulador: el coste real de
estos cambios en Old se mide con el recorrido del modo de prueba en consola.
