# Inventario del port SM64DS — 21 de octubre de 2026

Actualización del 22 de octubre: los arreglos e informes posteriores a esta
fotografía inicial se registran en [la pasada de código y rendimiento](CODE_PASS_2026-10-22.md)
y en [la matriz de pruebas](PLAN_PRUEBAS_2026-10-22.md). Ya se localizó el SDK,
se generaron candidatos ARM completos y comenzó la validación real en Azahar.
Las referencias históricas de abajo conservan su fecha y alcance.

El port tiene una **base nativa jugable y una integración avanzada**, pero todavía
no una versión final aceptada de principio a fin. El trabajo pendiente combina
correcciones conocidas, funciones por terminar y validación en consola. No hay
base suficiente para asignar un porcentaje global de finalización.

Este inventario contrasta el código local, los documentos más recientes, los
paquetes conservados y las pruebas ejecutadas el 21 de octubre. No se lanzó el
juego ni se probó una consola durante esta revisión. «Documentado en Azahar»
significa evidencia histórica del candidato indicado, no una prueba nueva de
las fuentes actuales. Las tareas de abajo son la propuesta de continuación;
no se han cambiado las mecánicas del juego en esta revisión.

## 1. Qué versión estamos inventariando

| Línea | Situación | Papel actual |
| --- | --- | --- |
| Visor histórico `sm64ds-preview` | Carga y dibuja el castillo; no es la aventura | Antecedente y herramientas de bajo nivel |
| Runtime experimental `sm64ds-native` | Desarrollo previo de arranque, ABI, archivos, jugador y servicios DS | Referencia técnica; sus pendientes no describen todo el juego integrado |
| Juego integrado ROM-only | Base nativa de Sébastien Breton más las adaptaciones de este proyecto | **Línea principal que debemos terminar** |
| Movement Lab | Banco separado de movimiento, presentación y colisiones | Pruebas auxiliares; sus resultados no sustituyen aceptación del juego principal |

La dependencia nativa está fijada en `88725627c529e5d9db57abc5544513d8741fa07d`
y su decompilación en `d3022c1463a260ba67a3dc148aea01eb8de92ca3`.
Los **768 archivos** del manifiesto del proveedor coinciden hoy con sus hashes.
Las adaptaciones se aplican a copias generadas; no hace falta empezar de nuevo
ni sustituir toda la dependencia.

### Fuentes, candidatos y paquetes no equivalen

- El último candidato completo documentado es **`eb76eabaf6f4e95c`**, del 19 de
  octubre: reutilización de uniformes y dos sesiones Azahar, Old/castillo-Yoshi
  y New/Bob-Mario, con tres capturas documentadas por sesión. Esto no cierra
  el resto de escenarios ni las pruebas físicas.
- La fuente incluye también reutilización de descriptores de vértices,
  conectada al preparador y al build. Su equivalencia host pasa hoy. No se
  encontró evidencia de un candidato completo posterior que permita atribuir
  esta segunda optimización al ejecutable anterior.
- El paquete ROM-only más reciente por fecha conservado en `dist/` es
  **`95f2bc484896ada1`**, del 18 de octubre. Su CIA coincide con el SHA-256 de
  su `verification.json`. Es anterior a varias correcciones de Options y al
  renderer del día 19; **no debe confundirse con el estado actual de fuentes**.
- Antes de esta revisión no existía `build/` en esta copia. Los informes y
  candidatos históricos citados allí no están disponibles localmente para
  repetir su auditoría. Sí hay capturas y recibos de UI en `assets/proposals/`.
- No hay commits en el repositorio raíz y hay desarrollo preparado, modificado
  y sin seguimiento. Se ha preservado. Falta una referencia versionada que
  haga reproducible el siguiente candidato.
- No está instalado devkitPro en la ruta `/opt/devkitpro` que usan varias
  herramientas ARM. El build completo de 3DS no se ejecutó en esta revisión.
  También falta la ROM en la ruta predeterminada `build/roms/europe.nds`;
  hay entradas privadas en la raíz cuya identidad debe comprobarse al preparar
  el build, sin deducir región por el nombre.

## 2. Lo que ya existe y hasta dónde está comprobado

«Existe» no significa «aceptado en todas las situaciones». La última columna
identifica la parte que todavía impide cerrar cada sistema.

| Sistema | Ya está | Todavía falta |
| --- | --- | --- |
| Ejecución nativa | Código del juego, actores, overlays, servicios de plataforma y arranque integrado | Regresión amplia del ejecutable final; no es un juego completo validado por haber enlazado |
| ROM-only | Descubrimiento de `.nds`/`.NDS` con nombre libre en `SD:/3ds/sm64ds/`, validación y preparación local | Repetir instalación limpia y recuperación en hardware con el candidato final |
| Regiones | Perfil europeo ASMP revisión 0 | Otras regiones/revisiones no están soportadas por esta instalación; el visor USA antiguo no amplía esa compatibilidad |
| Contenedor de recursos | Lectura directa de `.sm64ds`, 2.450 miembros; evita miles de archivos en SD | Regresión final de caché, interrupciones y errores de almacenamiento |
| Packer de escritorio | Conversión local, arrastrar ROM, elegir destino, progreso, cancelar y verificar; fuentes para Mac/Windows/Linux | Aceptación de GUI y paquetes actuales por plataforma; los launchers no equivalen a binarios nativos publicados |
| Pantalla de preparación | Arte original, progreso y seis idiomas con inglés por defecto | Aceptación física del diseño y mensajes; los seis idiomas son del cargador, no una afirmación de traducción de todos los menús del port |
| Tapa y energía | Política de preparación con tapa cerrada, control de pantalla y aviso LED | Verificar tapa, retroiluminación, LED y restauración de suspensión en Old/New reales |
| Arranque y aventura | Título, archivos, introducción, entrada a Stage y juego con movimiento/salto documentados | Campaña completa, jefes, estrellas, desbloqueos, final y transiciones |
| Personajes | Mario, Luigi, Wario y Yoshi; carga de modelos, animación y cambios de personaje | Matriz real de habilidades, transformaciones y progresión con los cuatro |
| Circle Pad y botones | Movimiento analógico en Standard/Touch/Dual-hand; Buttons separado de Control | Cruces de todos los modos y acciones en el ejecutable final y mandos físicos |
| Cámara | C-Stick libre, sensibilidad y ruta de arrastre con stylus para Old3DS | Validación real de cuatro direcciones, límites, colisiones, menús y stylus Old |
| Física Original | Ruta DS original conservada; modo predeterminado | Regresión global; es la referencia estable frente a los fallos reportados de Hybrid/N64 |
| Física Hybrid/Nintendo64 | Solver real integrado en el juego, persistencia de modo y cesión a DS para acciones/habilidades | Fallos reportados de triple salto, locomoción bloqueada y polvo; aceptación reabierta |
| Colisiones adaptadas | Suelo, pendientes, paredes, techos, contactos estables y transporte de plataformas; pruebas extensas por componente | Escenas, plataformas, efectos de superficie y cambios de propietario en juego real |
| Options y pausa | Menú nativo 320×240 compartido entre llave inglesa y pausa; Controller, Audio, Display, Developer y Update; aceptar/cancelar | Pantalla superior intermitentemente negra, parpadeo de OK y ciclos prolongados de navegación |
| Aspecto de Options | Botones naranjas de categoría, marcos aprobados, estrella pequeña, flechas, paginación y confirmación KEEP/DELETE | Aceptación visual física; no reintroducir diseños ya rechazados |
| HUD inferior | La integración actual conserva los sprites/mapa originales; el atlas recreado rechazado está excluido | Integrar el diseño original aprobado y sus dos botones de cámara con regiones táctiles; estados dinámicos |
| Render y efectos | GPU nativa, geometría, texturas, iluminación, sombras, niebla, alfa, composición 2D y capturas DS | Regresión de efectos/transiciones; título negro/gris y aspecto de sprites superiores en Wide |
| Resolución y encuadre | Original/Native y Original/Wide/Stretch; 400×240 Native/Wide documentado, culling horizontal y diagnósticos del destino real | Aplicación durante pausa, intro/inicio, márgenes 4:3 y bordes; existen rutas que vuelven a resolución DS por compatibilidad |
| Estéreo | Ruta de dos ojos y administración de recursos mono/estéreo | Slider, cambios de escena, efectos, rendimiento y aceptación física |
| Presentación a 60 Hz | Fotogramas visuales intermedios sin acelerar la simulación original | **60 FPS sostenidos en Old y New no están demostrados** |
| Optimización | Cachés de materiales/topología, reutilización de uniformes y descriptores, reducción de repintados | Medición comparable por escena; reducir operaciones no demuestra el mismo porcentaje de mejora en FPS |
| Audio | SDAT/DSP, mezcla, modos Surround/Headphones/Stereo y volumen 0–200 % | Crujidos reportados en consola; continuidad, latencia, saturación y prueba audible física |
| Guardado | HAL y operaciones originales de inicializar, copiar, borrar y recargar; pruebas host pasan hoy | Guardar durante juego, cerrar, reabrir y continuar en hardware; durabilidad ante interrupción no demostrada |
| Minijuegos | Rec Room y catálogo de 30 fábricas explícitas que agrupan variantes; el proveedor anuncia 36 juegos | Verificar cada juego/variante, táctil, puntuación, salida y persistencia en esta integración |
| VS / Download Play | Ruta de rechazo controlado hacia el título | **Multijugador inalámbrico no está integrado**; requiere trabajo propio si forma parte del alcance final |
| Diagnósticos | FPS/tiempos, overlay Select, capturas RAM/GPU, borrado con confirmación y transferencia VRAM por bloques de 64 KiB | Repetir el fallo físico VRAM/FS y comprobar entrada/retorno sin bloquear controles |
| Update | Página visible e inerte | **Actualizador real no implementado**: consulta, descarga, verificación, instalación y recuperación |
| Distribución | Herramientas CIA/3DSX, verificación, arte de icono/banner y paquetes experimentales | Paquete final de las fuentes actuales, prueba HOME Menu, instrucciones y enlace/QR del mismo binario |

## 3. Lista de trabajo, en el orden propuesto

Las casillas siguientes son trabajo **abierto**. «Verificar» indica que hay
implementación; no implica programarla otra vez. Cada ID permite referirse a
una tarea sin reabrir toda la historia del proyecto.

### A. Recuperar un punto de partida reproducible

- [ ] **BASE-01 — Preparar el entorno de compilación ARM.** Resolver devkitPro,
  herramientas y rutas de esta máquina; verificar la ROM privada admitida.
  Cierre: comando documentado que genera y audita un candidato completo.
- [ ] **BASE-02 — Fijar fuentes y binario de referencia.** Conservar el desarrollo
  actual, generar un manifiesto y separar visor, laboratorio, candidato y entrega.
  Cierre: cada prueba identifica hash de fuentes, ELF, 3DSX y CIA correspondientes.
- [ ] **BASE-03 — Actualizar documentación de entrada.** Separar el estado actual
  de los diarios históricos y recuperar los informes originales si están en otra
  copia. Cierre: README/estado/paquete no presentan versiones distintas como actuales.

### B. Terminar HUD, Options y cámara: primera tanda funcional

- [ ] **UI-01 — Integrar el HUD original aprobado.** Usar sprites originales,
  mapa conservado y dos botones izquierda/derecha. La revisión posterior de
  píxeles enteros usa marcos originales 32×32, sustituyendo las propuestas de
  flechas escaladas. Cierre: dibujo y regiones táctiles coinciden en juego real.
- [ ] **UI-02 — Validar todos los estados del HUD.** Contadores, retratos,
  gorras/marcadores, pulsación, prioridad, oclusión y efectos. Cierre: cambios
  de personaje/escena no dejan piezas antiguas ni sprites desaparecidos.
- [ ] **UI-03 — Corregir el título superior intermitentemente negro.** Cierre:
  abrir, navegar, aceptar/cancelar y reabrir Options conserva el título.
- [ ] **UI-04 — Cerrar la regresión de navegación.** Repetir ciclos desde llave
  inglesa y pausa, entrar a Adventure/archivo, Controller/Buttons/Control,
  Developer y confirmación. Cierre: sin pulsaciones filtradas al menú inferior,
  lentitud acumulada, pérdida de foco o diseños descartados.
- [ ] **UI-05 — Quitar parpadeo de OK y resolver cambios de Display en pausa.**
  Cierre: cambio visible y coherente con lo anunciado, preservando nivel, mapa
  y estadísticas superiores; si se mantiene aplicación al reanudar, explicitarlo.
- [ ] **CAM-01 — Aceptar stylus Old3DS.** Primer contacto, arrastre, soltar,
  recontactar, bordes y transición a diálogos/menús. Cierre: no hay salto inicial
  ni interferencia con los botones de cámara o el resto de entrada táctil.
- [ ] **CAM-02 — Aceptar cámara y sensibilidades físicas.** C-Stick en cuatro
  ejes, límites de suelo/techo/pared y Circle Pad. Cierre: sensibilidad visible
  consistente y control sin ajustes internos innecesarios en la interfaz.
- [ ] **UI-06 — Pulir omisión de la intro.** Ya existe primer Start para aviso
  y segundo Start tras soltar para omitir. Cierre: transición blanca tapa la
  desaparición de actores y entrega correctamente el control a Yoshi.

### C. Sonido, guardado y estabilidad

- [ ] **AUD-01 — Diagnosticar y corregir crujidos físicos.** Medir falta de
  muestras, discontinuidades y saturación antes de cambiar mezcla/búferes.
  Cierre: audio continuo en menú, intro, movimiento, pausa y carga.
- [ ] **AUD-02 — Validar modos y volumen.** Probar 0/100/200 %, altavoces y
  auriculares, con música y efectos. Cierre: silencio real en 0, cambios correctos
  y ninguna distorsión inesperada atribuible al control de ganancia.
- [ ] **SAVE-01 — Guardar y continuar una partida real.** Obtener progreso,
  usar el guardado original, salir, volver a abrir y cargar. Cierre: mismo archivo,
  estrellas/progreso y ajustes después del reinicio, en Old y New.
- [ ] **SAVE-02 — Verificar operaciones y errores de SD.** Copiar/borrar slots
  de prueba, reemplazo de guardado, SD llena y fallos de escritura. Cierre:
  errores visibles y conservación de datos conforme a la política elegida.
- [ ] **DBG-01 — Reprobar el fallo físico de dump VRAM/FS.** La corrección por
  bloques existe. Cierre: captura completa en la consola afectada, sin aborto
  FS y con retorno normal; conservar los artefactos originales del usuario.
- [ ] **DBG-02 — Cerrar diagnósticos en vivo.** Select abre/cierra, movimiento,
  salto y cámara siguen activos; Dump RAM y Clean funcionan con su confirmación.
  Cierre: prueba en el binario final con archivos desechables.
- [ ] **LIFE-01 — Tapa, HOME, suspensión y retorno.** Separar preparación y
  juego; comprobar liberación/reanudación de recursos, pantallas y DSP.
  Cierre: ciclos repetidos en Old/New sin cuelgues ni pérdida de entrada/audio.

### D. Reparar y aceptar Hybrid/Nintendo64

- [ ] **MOV-01 — Reproducir y corregir el encadenamiento del triple salto.**
  Comparar estados y pulsaciones con Original; no basta que el solver avance.
- [ ] **MOV-02 — Eliminar locomoción bloqueada.** Probar reposo prolongado,
  despertar, caminar/correr, aterrizar y cambiar de modo sin perder control.
- [ ] **MOV-03 — Recuperar presentación del movimiento.** Polvo del suelo,
  animaciones, pisadas, voces y sincronización visual según acción/superficie.
- [ ] **MOV-04 — Aceptar cesiones entre solver y DS.** Saltos mantenidos de
  Luigi/Yoshi, ataques, lengua/objetos, transformaciones, agua, daño, diálogos
  y scripts. Cierre: una sola autoridad de posición y regreso limpio al modo.
- [ ] **MOV-05 — Aceptar colisión y plataformas en vivo.** Pendientes,
  esquinas, paredes, techos, geometría degenerada, plataformas móviles/escaladas,
  desaparición de actores y cambio de nivel. Cierre: sin atravesar, atascar,
  transportar mal o conservar contactos caducados.
- [ ] **MOV-06 — Matriz de controles y personajes.** Original/Hybrid/N64 ×
  Standard/Touch/Dual-hand × cuatro personajes. Cierre: acciones correctas y
  ajustes independientes; cada fallo reproducible debe tener su caso de regresión.

### E. Gráficos, resolución y estéreo

- [ ] **GFX-01 — Corregir proporciones de sprites superiores en Wide.**
  Cierre: menús/HUD no se ensanchan indebidamente al ampliar la vista 3D.
- [ ] **GFX-02 — Inspeccionar Native desde arranque e introducción.** Registrar
  dimensiones reales y motivo de retorno a DS. Cierre: implementar lo que falte
  sin romper feedback, ventanas o alfa; documentar excepciones compatibles.
- [ ] **GFX-03 — Aceptar encuadre y culling.** Márgenes negros 4:3 sostenidos,
  ambos bordes Wide y rutas de geometría de respaldo. Cierre: no desaparecen
  actores/efectos que deberían ser visibles.
- [ ] **GFX-04 — Cerrar regresión visual completa.** Estrella inicial negra/gris,
  sombras, niebla, agua, transparencias, partículas, fades y cambios de escena.
- [ ] **GFX-05 — Aceptar estéreo físico.** Mono→dos ojos→mono, profundidad,
  efectos, escenas y presión de memoria; medir New 2D y New estéreo por separado.
- [ ] **GFX-06 — Integrar y comprobar la última caché de vértices en candidato.**
  El test host actual pasa; falta prueba de ejecutable del estado exacto de fuentes.

### F. Cobertura del juego completo

- [ ] **GAME-01 — Recorrido de aventura.** Crear matriz de cursos/áreas,
  misiones/estrellas, jefes, llaves/puertas, desbloqueos de personajes, muerte,
  Game Over, reentrada y final. Cierre: registro de casos, no solo arranque de niveles.
- [ ] **GAME-02 — Rec Room.** Probar todos los minijuegos/variantes incluidos,
  controles táctiles, puntuaciones, reinicio/salida y guardado. Su inclusión en
  el catálogo no demuestra que todos funcionen con las modificaciones actuales.
- [ ] **GAME-03 — Resolver el alcance de VS/Download Play.** Hoy no existe
  integración inalámbrica. Si se busca paridad completa, diseñar/implementar y
  probar conexión entre consolas; si se entrega solo un jugador, declararlo
  expresamente. No bloquear la aventura estable con una promesa implícita de VS.

### G. Rendimiento y memoria, después de las regresiones funcionales

- [ ] **PERF-01 — Ejecutar matriz final sobre el mismo binario.** Cinco
  escenarios en Old/New: castillo/Yoshi, Bob/Mario, Whomp/Luigi, Bay/Wario y
  Clock/Wario; tres capturas válidas por combinación y entrada real.
  Cierre: 10 ejecuciones/30 capturas comparables. La matriz histórica mezcla
  candidatos y no satisface este criterio.
- [ ] **PERF-02 — Medir hardware.** Tiempos completos mediana/p95/p99,
  CPU/GPU/esperas, I/O, memoria y cortes de audio. Cierre: Old, New 2D y estéreo
  caracterizados por separado; no sumar ámbitos temporales que se superponen.
- [ ] **PERF-03 — Optimizar costes medidos.** Reutilización de geometría,
  atributos/texturas, visibilidad y contención de workers según evidencia.
  Cierre: comparación antes/después en la misma escena sin cambiar jugabilidad.
- [ ] **PERF-04 — Resolver el objetivo de 60 FPS sostenidos.** Mantener el
  ritmo original de simulación. Cierre: medición física sostenida en los perfiles
  acordados; presentar a 60 Hz o reducir uniformes no cierra la tarea.
- [ ] **PERF-05 — Memoria y sesiones largas.** Repetir cargas, pausas, cambios
  de resolución/estéreo/escena, muertes y minijuegos. Cierre: sin crecimiento
  acumulativo, agotamiento ni cierres; investigar también el cierre histórico
  New/Whomp cuya causa quedó sin resolver.

### H. Instalación y entrega final

- [ ] **SETUP-01 — Instalación limpia y reutilización.** ROM admitida con nombres
  distintos, `.NDS`, contenedor directo, caché existente y ausencia de ROM;
  errores/cancelación/interrupción sin residuos o pérdida del original.
- [ ] **SETUP-02 — Aceptación del cargador y Packer.** Seis idiomas y fallback,
  mensajes, logo, progreso, cancelar, arrastrar/guardar y plataformas de escritorio.
- [ ] **REL-01 — Revisar cambios útiles de la decompilación.** Comparar con
  revisiones fijadas e importar solo cambios justificados; 100 % matched no
  significa port 3DS completo ni rendimiento resuelto.
- [ ] **REL-02 — Icono y banner.** Conservar arte aprobado y comprobarlo en
  HOME Menu físico; el verificador de CIA no demuestra su aspecto en pantalla.
- [ ] **REL-03 — Paquete final CIA/3DSX.** Mismo candidato aprobado, hashes,
  créditos/licencias, instrucciones de instalación/compatibilidad, sin datos
  privados del juego; entrega local y enlace/QR verificado cuando corresponda.
- [ ] **EXTRA-01 — Actualizador real.** Función todavía ausente. Definir fuente
  de versiones, verificación y recuperación antes de convertir Update en acción.
- [ ] **EXTRA-02 — Otras regiones/revisiones.** Ampliación opcional; requiere
  manifiestos y pruebas propias. No está implícita en aceptar cualquier nombre `.nds`.

## 4. Comprobaciones ejecutadas hoy

- **16/16 tests CTest del host pasan** en Debug. Cubren datos, matemáticas,
  geometría, materiales, entrada, archivos, NARC, capturas y colisiones básicas.
  Las rutas opcionales que requieren argumentos/recursos externos no se
  ejecutaron por el mero hecho de pasar CTest.
- **14/14 grupos de componentes pasan con ASan/UBSan**: menú de pausa actual,
  C-Stick/Circle Pad, stylus, plan de pantalla, omisión de intro, overlay,
  transferencia de dumps, tapa/energía con servicios simulados, planificador
  de benchmark, raycast, botones de movimiento, contactos estables,
  plataformas estables y reintentos.
- **2/2 pruebas de renderer pasan con ASan/UBSan**. Uniformes: 441.600
  comparaciones equivalentes. Descriptores de vértices: 288.266 estados de
  dibujo comparados, con caché activada y desactivada. Son pruebas sintéticas,
  no mediciones de FPS ni una comparación de imágenes de consola.
- **Guardado host pasa con ASan/UBSan**: crear/reemplazar/reabrir, bloques
  espejo/recuperación y comandos originales de inicialización, seis copias,
  tres borrados y recarga. Solo archivos temporales; no se tocó una partida real.
- **Hooks de pausa: cinco pruebas pasan y una se omite** según las condiciones
  del test existente. No se cuenta la omitida como validación completada.
- **768/768 hashes del proveedor coinciden** y el CIA conservado `95f2bc…`
  coincide con su hash registrado.

Los dos tests de contactos/plataformas necesitaron corregir la ruta de include
de la invocación de este inventario; después pasaron sin modificar fuentes.
No se interpretó ese error de invocación como un fallo del port.

Resultados nuevos locales: `build/inventory-2026-10-21/ctest.xml`,
`build/inventory-2026-10-21/host-components.json`,
`build/integrated-native/mesh-uniform-gate/report.json`,
`build/integrated-native/vertex-input-gate/report.json` y
`build/integrated-native/save-host-1792630412437753000/report.json`.

No se hizo build ARM completo, ejecución Azahar nueva, prueba física ni
aceptación del juego completo. Los tests host que pasan no cierran las
regresiones de comportamiento reportadas en consola.

## 5. Fuentes y discrepancias resueltas

- [Checklist histórico y últimas regresiones](CURRENT_WORK_CHECKLIST.md).
- [Último estudio e integración del renderer](NP49_RENDERER_STUDY.md).
- [HUD original, stylus y Options](HUD_CAMERA_FRONT_OPTIONS_REVISION.md).
- [Cronología de movimiento](MOVEMENT_LAB.md) e
  [integración de menús](IN_GAME_OPTIONS_PROGRESS.md).
- [Campaña de rendimiento](PERFORMANCE_CAMPAIGN.md),
  [guardado e integración](INTEGRATED_NATIVE.md),
  [instalación ROM-only](ROM_ONLY_INSTALL.md) y [Packer](SM64DS_PACKER.md).
- Código actual: `tools/prepare_integrated_movement.py`,
  `source/integrated_native/movement/native_motion.cpp`,
  `tools/prepare_integrated_native_ui.py`, `tools/prepare_integrated_camera.py`,
  `source/integrated_native/options/native_pause_menu.cpp`,
  `tools/prepare_integrated_vertex_inputs.py` y `tools/prepare_integrated_mesh_uniforms.py`.
- Recibos/capturas conservados:
  `assets/proposals/options-action-buttons-2026-10-18/verification.json`.
- Alcance de escenas/minijuegos:
  `third_party/sm64ds-3ds/port/platform/3ds/game_main.cpp` y
  `third_party/sm64ds-3ds/port/menu/minigame_imports.inc`.

Correcciones de lectura importantes: Hybrid/N64 **sí están integrados**, aunque
su comportamiento sigue abierto; suelos/paredes/techos/plataformas **ya tienen
implementación**, aunque algunas casillas antiguas aún digan lo contrario; el
HUD recreado anterior **no es el diseño vigente**; y un paquete de `dist/`
**no representa automáticamente las fuentes actuales**. Las cifras o estados
de upstream aquí citados son los conservados en la copia fijada, no una consulta
al estado público de hoy.
