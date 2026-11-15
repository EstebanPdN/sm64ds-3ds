# Presupuesto nativo de 60 FPS — Old y New 3DS

## Objetivo activo y condición de la próxima CIA

Última instrucción explícita del propietario, 2026-10-29: dejar el juego
entero 100% nativo de 3DS y completar primero el render 2D, eliminando el
puente PPU. No compilar otra CIA hasta completar la migración. BUILD 42 ya
estaba empaquetada antes de esta instrucción; su validación instalada se
interrumpió para priorizar el objetivo nuevo y se cerró la única instancia.
La siguiente entrega debe conservar la mejora de sincronización y pasar por
menú/cabeza, texto/scroll, HUD/mapas, efectos/ventanas y transiciones, además
de la auditoría de las dependencias DS restantes en gráficos y lógica.

«100% nativo» no se declara por compilar C/C++ para ARM11 ni por renombrar
el PPU. Requiere productores semánticos y recursos propios, envío PICA directo,
ausencia de rasterización/captura 2D desde VRAM/OAM emuladas en la ruta de
juego y reemplazo de los servicios simulados que aún observe la lógica.

Objetivo actual: 60 imágenes por segundo, con margen bajo 16,67 ms por
imagen en ambos modelos. Bob-omb Battlefield es la escena obligatoria.
Las medias no cierran el objetivo: deben comprobarse intervalos, picos,
movimiento, cámaras, enemigos, explosiones y cargas del recorrido.
La aceptación física permanece pendiente en ambas consolas.

## Estado comprobado el 2026-10-29

BUILD 35 resolvió el lienzo. BUILD 36 introdujo emparejamiento por identidad
(actor con generación + componente + grupo de skin), historial propio de
poses y vértices animados, cuadro intermedio y extremo en VBlanks alternos.
La lógica original conserva su cadencia. Old usa render en core 0 y audio
en core 1; New solicita render en core 2. libctru documenta un solo hilo de
aplicación posible en core 1. La afinidad real requiere prueba física.

BUILD 37 contabiliza también el extremo en el contador visible. Antes,
se renderizaban cuadros adicionales pero se contaban únicamente los cuadros
base: el 29,9 no demostraba por sí solo un límite de presentación.
Los contadores de llamadas del render tampoco demuestran scanout físico.

BUILD 37 añade una entrada de diagnóstico reproducible:
`SM64DS_NATIVE_COURSE=bob`. Usa `SaveData::SetCharacterIntro(7)`,
`StartFile(6,0)` y `Player::SetRealCharacter(0)`, igual que las APIs verificadas
en la campaña anterior. No modifica RAM arbitrariamente, desbloqueos ni
partidas. El registro identifica nivel, entrada y personaje. Se debe cerrar
el diálogo introductorio antes de la medición del recorrido libre.

Se observaron Mario, escenario y mapa de Bob-omb en la UI de Azahar 2126.2
para ambos modelos. New incluye movimiento hacia el cañón; todavía no es
el recorrido completo. Pruebas secuenciales, sin compilación simultánea ni
capturas completas de RAM. Evidencia privada:

- `build/native-renderer/playable-r1/sessions/bob-p37-new/`
- `build/native-renderer/playable-r1/sessions/bob-p37-old/`

Cada carpeta contiene `presentation-baseline.json`, logs, configuración y
capturas reales de UI. New tiene 80 ventanas de 120 imágenes; Old tiene 47.
Incluyen calentamiento y diálogos; no se presentan como percentiles por
cuadro ni se extrapolan a hardware.

Las últimas diez ventanas de New registran 59,51–59,61 llamadas de
presentación/s, un máximo de intervalo de 40,680 ms y de llamada de 19,972 ms.
Las de Old registran 59,17–59,64/s, intervalo máximo 42,276 ms y llamada máxima
19,605 ms. El tiempo de llamada incluye preparación y esperas, no es tiempo
puro de CPU. Estos resultados NO cumplen estabilidad de 60 FPS con margen.

BUILD 38 prueba `C3D_FRAME_SYNCDRAW` en ambas imágenes interpoladas para
regular la cadencia, conservando la ruta anterior en
`SM64DS_NATIVE_SYNC_INTERPOLATION=0` para comparación. Esta hipótesis aún
requiere medición: no se atribuye una mejora a haberla compilado.

## Costes y trabajo restante

Medir separadamente lógica/captura, modelos, skin, PPU 2D, interpolación y
copias, envío, GPU, transferencias, esperas y SD. CPU/GPU se solapan: no sumar
sus tiempos como si todos fueran secuenciales. Separar carga inicial y picos
del recorrido; conservar ambos resultados. Verificar imágenes efectivamente
presentadas además de llamadas del renderer.

El censo de BUILD 37 confirma accesos a registros DS de 2D, IRQ, división,
raíz cuadrada y configuración gráfica. Los modelos/poses/skin llegan por
productores semánticos nativos, pero la ruta completa aún conserva traducción.
La lógica C/C++ ya se compila para ARM11 (ARMv6K/mpcore, hard-float, -O2);
eso no elimina automáticamente sus accesos a periféricos DS simulados.

Eliminar esas rutas por propietarios semánticos: matemática ARM11 con
redondeo y casos límite verificados, temporización/IRQ nativas, 2D directo y
servicios nativos. Geometría estática persistente en GPU, estado dinámico
mínimo y batching deben reducir trabajo por cuadro conservando orden,
transparencia, visibilidad y paridad. Conservar pruebas físicas independientes
para Old y New y CIA numerada al validar cada hito.

## Una sola instancia

`launch_playable.py` comprueba procesos vivos y serializa preparaciones con
un bloqueo. Se probó su rechazo ante otro Azahar activo. Antes de otra sesión
se confirma la salida de la anterior. No adjuntar con CUA una ruta de app
parada: podría arrancarla nuevamente.

## Observación inicial de BUILD 38 / New

Una sola instancia. UI real: Azahar alcanzó 60 FPS mientras el contador del
juego indicaba 59,8. Durante una muestra de movimiento se observaron 58 y
54,0 respectivamente. La sincronización reduce muchos intervalos largos,
pero no se acepta todavía como estabilidad con margen. Se conserva la serie
completa en `sessions/bob-p38-new/presentation-baseline.json`, incluidos
calentamiento, diálogos e input manual. El registro Old de BUILD 38 terminó
con 4.567 ventanas; las últimas diez están entre 53,94 y 55,20/s y llegan a
41,984 ms de intervalo. Incluye ejecución prolongada y no es un recorrido
controlado; no se usa para porcentajes de mejora.
Los contadores de tiempo de llamada incluyen la espera de sincronización.

## BUILD 39–40: rutas compartidas, sin excepciones por nivel

BUILD 39 conecta cinco consultas síncronas a la matemática ARM11 existente:
longitud s32/s16, distancia, raíz escalada y distancia de audio. No pasan por
registros DS ni deshabilitan IRQ simuladas. La división asíncrona conserva
su propietario actual: PerspectiveW y otras hojas todavía observan su estado.
`SM64DS_NATIVE_MATH=0` permite una comparación explícita. No hay ramas por
identificador de nivel en estas sustituciones.

`tools/test_native_renderer_math_arm.py` compara las envolturas que se enlazan
en el juego con cinco hojas del ARM9 original: 5.215 casos, ABI, canarios,
entradas inmutables y cinco llamadas al propietario anterior al desactivar el
render nativo. Solo el oráculo ARM9 tiene registros DS mapeados. Resultado
privado con hashes: `build/native-renderer/math-arm/report.json`.

BUILD 40 elimina tres costes comunes:

- SceneFrame prepara cada parte una vez en su cola todavía no publicada.
  Un rechazo revierte la paleta y no publica la cola parcial. Los clientes
  que retienen su salida conservan la operación transaccional anterior.
- LightingCache reempaqueta únicamente las 15 filas de normales cuando solo
  cambia un hueso y conserva luces, material y tabla ya admitidos.
- La subida de imagen 2D convierte cada píxel una vez y escribe sus cuatro
  réplicas Morton contiguas. Conserva exactamente muestreo, alpha, orientación
  y relleno. Tras la inicialización solo vacía la región escrita de 768 KiB,
  frente al MiB completo anterior. Esto optimiza el puente temporal; no lo
  convierte en un productor 2D nativo.

Las 47 pruebas host pasan, incluyendo rechazo tardío seguido de publicación
válida, 1.000 cambios de normal contra el empaquetado completo, pérdida de
residencia y dos imágenes completas verificadas con un decodificador Morton
independiente y canarios. Compilación ARM11 y enlace de BUILD 40 completados.

El perfil nuevo registra ambos tipos de imagen y percentiles por ventana de
120 intervalos. `activeWall` resta únicamente la espera medida en FrameBegin;
todavía puede contener preempciones. `previousGpu` corresponde a la cola
anterior. No sumar estos tiempos al tiempo de actualización como si fueran
trabajo secuencial. Se conserva también el intervalo que cruza las ventanas.

Pruebas secuenciales de BUILD 40 en Azahar, receta de entrada en cada env.txt:

- `sessions/bob-p40-old/`: 61 ventanas posteriores al frame de fuente 2.100,
  39,53–59,89 llamadas/s; intervalo máximo 72,227 ms.
- `sessions/bob-p40-new/`: 52 ventanas posteriores al frame de fuente 2.100,
  51,97–59,87 llamadas/s; intervalo máximo 70,176 ms.

Son recorridos con entrada, movimiento, cámara y enemigos; no una prueba de
igualdad de trayectoria entre modelos ni aceptación de todos los niveles.
Las capturas reales confirman escenario, Mario y HUD. No cumplen aún el
objetivo completo de estabilidad. Las medias altas de algunas ventanas no
ocultan las ventanas lentas. `presentation-baseline.json` conserva todos los
registros, incluida carga y diálogo. La sesión exploratoria Old de BUILD 39
incluye teclas inicialmente mal codificadas (el formato es hexadecimal),
entrada al menú de control y movimiento manual: no sirve como comparación
controlada de FPS contra BUILD 40.

El escalón de aproximadamente 59,8 a 39,9 corresponde a dos imágenes por
dos o por tres refrescos. Los histogramas Old muestran muchas parejas que
ocupan tres refrescos. Queda por separar coste de trabajo y coordinación del
reloj original con C3D_FrameSync; el número por sí solo no identifica la causa
que hace perder ese refresco. La prueba de reloj único es diagnóstica y no
autoriza cambiar la velocidad de física ni retirar sus límites temporales.


## Diagnóstico de reloj único y paquete BUILD 40

`sessions/bob-p40-old-gpu-clock/` usa exactamente el binario BUILD 40 con
`PLAYER_OBSERVED_CADENCE=0` y `SM64DS_FRESH_NATIVE_PACE=1`. En las 24 ventanas
posteriores al frame de fuente 2.100 y anteriores al 3.600 registra
40,92–59,84 llamadas/s e intervalo máximo de 80,229 ms. Quitar el reloj de
software no elimina la caída durante el recorrido. No se adopta esa
configuración como arreglo ni se acepta su temporización física.
Se observó pausa y texto del curso con las rutas comunes. El servidor CUA
rechazó los toques posteriores con `noWindowsAvailable` aunque el proceso y
la captura seguían vivos; no se cuenta como una prueba de salida a otra zona.
La regresión entre niveles sigue pendiente.

Además de las 47 pruebas host, pasan bajo ASan/UBSan las comprobaciones de
imagen Morton, publicación de SceneFrame y luces/shader. La CIA privada
`hardware/build40/SM64DS-Native-BUILD40.cia` se empaqueta desde el mismo código
y bancos de la sesión New. El recibo incluye hashes, título de prueba
000400000F536500 y `hardware_validated=false`. El hito es reducir trabajo en
rutas comunes conservando resultados; no declara 60 FPS estables ni cierra
2D nativo, compatibilidad restante o aceptación física.

La CIA instalada arrancó en Azahar New y completó toque de estrella, título,
menú, cabeza de Mario, lápiz/lienzo sin franjas, retorno con Yoshi y selector
de tres archivos con su fondo original. El registro confirma captura de cabeza
correcta, render solicitado en core 2, audio en core 1 y ningún worker antiguo.
`hardware/build40/azahar-validation.json` conserva hashes de CIA, capturas y
log. Reiniciar la sesión CUA resolvió el error de automatización del toque;
no se cambió el mapeo de entrada del juego. Al terminar se cerró la única
instancia y se confirmó su salida. Otros niveles y consolas físicas siguen
pendientes; las pruebas de menú no son comparaciones de rendimiento.

## Referencia SoH: ARM11 selectivo

Objetivo activo añadido por petición del propietario: usar ensamblador
ARMv6K/ARM11 para reducir el coste nativo en Old y New 3DS, empezando por
rutinas compartidas con resultados exactos y medición propia. El primer
kernel es la composición de matrices afines Fix12 de modelos y huesos.
La aceptación requiere equivalencia/ABI y mejora medida; no sustituye los
pendientes de 2D nativo, cadencia, paridad visual ni los 60 FPS sostenidos.

Referencia solicitada por el propietario: `999sian/soh-3ds`, revisión pública
`0b93bad4242e1d532dabf78241c685546b16483f`, leída mediante GitHub. Su
`src/compat3ds/arm11/README.md` documenta kernels ARMv6K/VFPv2 de audio,
copias, matrices y emisión, con pruebas de ABI y equivalencia. Reporta una
reducción mediana de tiempo de 37,72% en resampling y 51,27% en copias de
matrices en una prueba física Old aislada. No son porcentajes de FPS del juego.
También documenta un seguimiento donde activar varios kernels no redujo el
tiempo total del renderer, y corrige atribuciones previas de sus problemas de
asignación. Las conclusiones se leen junto a esas correcciones, no solo sus
primeros resultados. El script canónico mantiene varios experimentos OFF.

Aplicabilidad al motor actual:

- Composición de poses y skin son candidatos compartidos por todos los niveles.
  Nuestro `game::compose` conserva productos y acumuladores enteros de 64 bits
  con reducción Fix12; los kernels flotantes SoH no lo sustituyen directamente.
  El desensamblado de BUILD 40 ya contiene SMLAL generado por GCC. Cualquier
  kernel propio debe mejorar su calendario/cargas, no limitarse a emplear esa
  instrucción, y conservar exactamente desbordamiento, redondeo y ABI.
- La conversión/subida de imágenes y copias de snapshots admiten investigación
  de cargas/almacenamientos por bloques. Primero evitar el trabajo completo
  que ya no necesita hacerse: BUILD 40 convierte cada píxel una vez.
- Los kernels de audio de SoH corresponden a su mezclador N64. Nuestro audio
  requiere sus propias identidades, formatos y perfil de hilo antes de adoptar
  una operación equivalente.
- El coste PPU/2D y de cambios de estado requiere productores y composición
  nativos, no traducir el bucle actual a ensamblador. Se mantiene como siguiente
  trabajo estructural. ASM es una optimización dirigida posterior o paralela
  para los kernels que resulten dominantes, no un requisito para que C/C++
  compilado ya sea nativo ARM11.

No se copió código del renderer SoH ni se adoptaron sus cifras como evidencia
de rendimiento de SM64DS. Cualquier incorporación tendrá medición propia en
Old y New y verificación sobre el binario final, además de las pruebas exactas.

## Primer kernel ARM11 — BUILD 41

`affine_compose_arm11.S` compone las matrices afines Fix12 compartidas por
modelos, huesos y sus bases de iluminación. Conserva los productos enteros
con acumulación módulo 2^64, los bits 12..43 del resultado y la suma de
traslación módulo 2^32. Usa SMULL/SMLAL y cargas por bloques ARMv6K, sin NEON,
periféricos DS, asignaciones ni coma flotante. Ambos modelos usan la misma ISA.
El contrato del kernel exige salida separada y alineación de palabra; el
propietario C++ conserva su semántica de asignación incluso con destino igual
a una entrada. La selección se fija antes de arrancar los workers.

`tools/test_native_affine_arm.py` compara 4.162 pares de matrices de borde y
aleatorias con referencia Python independiente, C de producción, ASM de
producción y bytes de la rutina original de la ROM del propietario. Incluye
entradas compartidas, asignaciones sobre cada entrada, cuatro alineaciones,
canarios y registros preservados por ABI. Pasan 16.648 llamadas C, 16.648 ASM,
16.648 originales y 8.324 directas al kernel: 58.268 en total.
`build/native-renderer/affine-arm/report.json` conserva hashes y resultados.

Las ocho muestras de ejecución ARM cuentan 505 instrucciones en la ruta C y
151 en ASM, incluyendo el selector de la fixture. Son instrucciones ejecutadas,
no ciclos, milisegundos ni mejora porcentual del juego. El ELF final BUILD 41
incluye el kernel y una llamada efectiva desde `game::compose`; el
desensamblado se conserva junto al ejecutable. Las 47 pruebas host y el oráculo
de cinco operaciones escalares siguen pasando. La comparación de juego usa
ese mismo binario y `SM64DS_NATIVE_ASM_MATRICES=0/1`, sin cambiar el escenario
ni los activos nativos entre ambas rutas.

La comparación Old BUILD 41 no establece una ganancia global de FPS atribuible
a ASM: ambas rutas reproducen el escalón de 39,9. En las 16 ventanas etiquetadas
entre fuente 2.100 y 3.119, C registra 39,73–59,84 llamadas/s y un intervalo
máximo de 86,947 ms; ASM, 40,92–59,84 y 89,325 ms. Son una ejecución por ruta,
con el mismo guion pero sin demostrar igualdad de estado/recorrido. No se
extrapola el conteo de instrucciones a ciclos ni al rendimiento físico.
`affine-build41-comparison.json` y las carpetas de ambas sesiones conservan los
datos. El propietario confirma que el bajón sigue visible con ASM.

## Sincronización de intercambio — BUILD 42

La fuente instalada de Citro3D confirma que `C3D_FRAME_SYNCDRAW` llama primero
a `C3D_FrameSync()`, que toma los contadores actuales y exige otro VBlank de
ambas pantallas. Una imagen que ya fue consumida durante la preparación CPU
no satisface esa espera. El nuevo camino drena la cola GPU mediante
`C3D_FrameBegin(0)` y espera mientras `gspIsPresentPending()` indique que el
intercambio de cualquiera de las dos pantallas sigue pendiente. Se conserva
la barrera de vida de recursos y se evita reemplazar un intercambio aún no
consumido. No se modifica la frecuencia de la lógica ni se aplica una excepción
por nivel. `SM64DS_NATIVE_PRESENT_FENCE=0` conserva el camino anterior para
comparación del mismo binario. La configuración por defecto usa la nueva ruta.

El reporte del propietario tras probar este cambio es: «Eso mejoró mucho».
Se registra como feedback sobre la versión emulada; la CIA solicitada permite
comprobarlo en hardware. La aceptación de 60 FPS estables en Old/New y de todos
los niveles sigue abierta.

## BUILD 134: el par de 60 en Old (sin medir en consola)

Datos de consola de BUILD 133 (Old 3DS, `dumps/prueba133`): actualización
12,2 ms, render 11,4 ms por imagen, GPU 8,75 ms por imagen, periodo 33,4 ms.
Render y juego comparten el núcleo 0; el audio vive en el núcleo 1 con el
30 %.

Por qué el par no cabía: después de la imagen intermedia, `C3D_FrameBegin`
de la exacta espera a que la GPU termine (8,75 ms) con el núcleo parado:
12,2 + 11,4 + 8,75 + 11,4 = 43,8 ms > 33,4.

Qué hace BUILD 134: la imagen exacta se graba en un segundo búfer de comandos
(cola GX propia, que no ejecuta nada) mientras la GPU dibuja la intermedia;
luego se añade a la cola del SDK, se espera a que la intermedia haya salido
por la LCD, se copia y se intercambia. Las esperas son tiempo bloqueado: el
hilo del juego puede usar el núcleo. Ciclo previsto con los números de BUILD
133: 12,2 + 11,4 + 11,4 = 35,0 ms, todavía por encima del periodo; con los
recortes de BUILD 134 (productores, 2D, audio, comandos) debería bajar, pero
cuánto solo lo dirá el recorrido en consola (`costo_imagen_ms`,
`costo_segunda_ms`, `presupuesto_cabe`).

Reglas:

- Lock FPS 30: una imagen exacta por actualización (por defecto en Old).
- Lock FPS 60 y Unlocked: par intermedia + exacta. Solo si actualización +
  las dos imágenes caben en el 92 % del periodo; si no, una imagen.
- Una actualización que encuentra el render ocupado con un par: 30
  actualizaciones a una imagen, duplicando hasta 480 si se repite.
- Entre las dos imágenes del par siempre pasa un refresco. Con 60, la
  intermedia espera además (al final de su trabajo de CPU) a que la exacta
  anterior esté en pantalla; con Unlocked no espera.
- New 3DS: el render tiene su núcleo; el camino no cambia respecto a BUILD
  133 salvo que el límite elegido ahora sí se aplica.

Lo que la imagen intermedia no interpola: sombras, quads 2D y bases de
normales (se mueven a 30 Hz).

Qué queda por recortar en la segunda imagen si la consola dice que no cabe:
repite validación, plan y la pasada de composición de la otra pantalla.

## BUILD 135: la segunda imagen del par, más barata (sin medir en consola)

La imagen exacta del par sale de la misma captura que la intermedia;
`PoseHistory::midpoint` solo sustituye las matrices de las partes y las
posiciones del skin. Ahora la segunda imagen:

- no vuelve a validar ni subir los quads, no reconstruye ni sube las sombras
  y no compara el mapa del motor B (son los mismos bytes que la intermedia
  acaba de subir; los planos 2D ya se saltaban desde BUILD 134);
- no valida ni dibuja la pantalla que no presenta: `submit_overlapped` solo
  transfiere la pantalla del 3D, así que la otra se quedaba en su render
  target sin llegar nunca a la LCD (menos CPU y menos GPU por par).

Lo que sigue repitiendo: validación de ocurrencias, residencia, subida del
skin (posiciones distintas), plan de lotes y dibujo. El plan no se reutiliza:
`NativeBatchPlan::append` agrupa por igualdad de matrices, y dos partes con
la misma matriz intermedia pueden tenerla distinta en la exacta.
