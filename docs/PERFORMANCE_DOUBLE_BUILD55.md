# Objetivo activo: al menos 2× respecto a BUILD 55

Solicitado por el propietario el 2026-10-29. La referencia queda congelada en
`game-p55/sm64ds.3dsx`, SHA-256
`2f6d1122ab4dcf72a20956ff1b6cf4a7030bd89f3aab6312f2f779f42186616d`.
No se cambia la referencia al mejorar una versión.

## Criterio

Conservar lógica, cadencia, recursos, resolución y fidelidad. Optimizar rutas
generales; no introducir excepciones por nivel o personaje. Objetivo: reducir
al menos a la mitad el coste activo por cuadro comparable en menú/cabeza y
Bob-omb Battlefield. No contar 60 FPS limitados por pantalla como prueba de
margen, ni reducir funciones, saltar dibujos o degradar calidad para aprobar.
P95/P99, máximos y cargas se informan aparte; no se ocultan bajo las medias.
La aceptación física de Old y New queda abierta hasta recibir mediciones de
consola. Azahar no tiene un factor de conversión fijo a hardware.

## Medición

Una sola instancia de Azahar. Compilar antes; verificar proceso cerrado antes
de la siguiente ejecución. Fijar binario, activos, opciones, script y duración.
Separar arranque/carga fría, diálogo, movimiento y estado sostenido. Las sondas
Bob anteriores de 900 actualizaciones sólo alcanzaban una ventana de entrada;
no constituyen la referencia sostenida del objetivo.

No sumar `models`, `skin`, `update`, `draw`, `submit`: varias medidas están
contenidas unas en otras, y los hilos pueden solaparse en New. Conservar el
coste por fase y los intervalos. Medir por separado cualquier instrumentación.

La corrida `double-baseline55-head-coarse-old` conserva la medición externa de
cada presentación, con perfil fino desactivado, 1.200 actualizaciones y un
único toque inicial. La cabeza sigue mostrando unos 9,1 ms por render en
Azahar. El perfil fino anterior costaba aproximadamente 0,8 ms adicionales;
retirar sondas no se contará como una duplicación del rendimiento del juego.
Con perfil fino apagado no se conoce el tiempo interno de espera por separado.

## Primera reducción posterior a BUILD 55

BUILD 56 envía sólo las filas de normales que cambian respecto al contenido
GPU residente. Se conserva comparación exacta por valor. Una invalidación,
primera iluminación o vuelta desde un estado sin luces obliga a restaurar las
12 filas. No se cambia el shader ni su precisión. La prueba mantiene una copia
independiente de las filas GPU, aplica únicamente las máscaras publicadas y
comprueba equivalencia y restauración completa.

Estado: pendiente de medir y aceptar. No se declara una mejora de FPS ni 2×
por reducir únicamente filas de uniformes.

## Resultado del experimento BUILD 56: descartado

53/53 pruebas y enlace ARM pasan; no hubo rechazos del render en 1.200
actualizaciones. Sin embargo, en comparación con BUILD 55 usando el mismo
guion y perfil fino apagado, enviar filas diferenciales reduce uniformes pero
sube el coste externo del render de la cabeza aproximadamente de 9,1 a 9,4 ms.
No se acepta como mejora de rendimiento. Se revirtió el código del experimento;
BUILD 56 y sus registros se conservan inmutables, y el siguiente número es 57.
Evidencia: `port-research-20261029/double-head-row-experiment.json` y sesiones
`double-baseline55-head-coarse-old` / `double-candidate56-head-coarse-old`.

El objetivo 2× sigue abierto. La ruta de gameplay todavía reactiva el puente
PPU al salir del menú (`sm64ds_fresh_native_scene_changed`); el menú nativo no
implica que HUD/mapas de gameplay ya sean nativos. La preparación 2D, las
llamadas por pieza y las cargas deben separarse en una referencia Bob sostenida.

## Precarga de la cabeza — BUILD 57, en validación

El propietario pregunta por precarga. La identidad del archivo se verificó
localmente en el índice de su ROM: 202 = `data/DSMT/face_demo_mario.bmd`;
el banco gráfico cocinado `00202.nmd` tiene 233.344 bytes. No es el tamaño total
de todas las estructuras de la cabeza ni una medida de memoria GPU final.

`SceneRenderer::preload_model` encola identidades del catálogo, con capacidad
fija de ocho y sin otro hilo. En la primera fase de subida, después de fijar
los recursos visibles, el propietario GPU carga geometría/texturas mediante
las mismas rutas y límites que una carga normal. El recurso precargado queda
en la caché LRU existente, no en una copia adicional. Un fallo especulativo se
registra y no rechaza la imagen visible. No se intenta cargar toda la ROM.
La petición de la cabeza se hace al inicializar el render; no espera al menú.
`SM64DS_NATIVE_PRELOAD_HEAD=0` permite comparación del mismo binario.

Esto adelanta I/O y subida gráficos al arranque; no elimina el coste sostenido
de animación/dibujo ni precarga aún los datos originales de lógica/animación.
No se anuncia como solución de la caída física a 7 FPS ni como cumplimiento de
2×. Se comprueba carga temprana, reutilización sin recarga al aparecer, memoria,
imagen y ausencia de rechazos antes de aceptar el mecanismo.

### Verificación de precarga

BUILD 57: 53 pruebas host y enlace ARM pasan. La ejecución Old de Azahar
`preload57-head-old` completa 1.200 actualizaciones sin rechazos ni fallos.
`Native bank begin: model=202 serial=1` seguido de `status=ok` aparece una sola
vez: la cabeza se conserva en la caché y no se recarga al revelarse.
Captura en frame 840 frente a BUILD 55: pantalla superior idéntica; inferior
sólo difiere en 24 píxeles de la etiqueta BUILD. Cabeza, poses, fondo y botones
coinciden. Evidencia: `preload57-validation.json`.
SHA-256 3DSX57: `463e9431f5f9413b3d230a9737845ef101aeb52da1047a5ab4dd077aa1598a23`.
No se empaquetó CIA nueva. La aparición sin tirón en Old física sigue pendiente.

La cola evita promover especulativamente un banco que la imagen visible use
sólo como texturas: esa promoción puede fallar tras liberar el banco anterior.
La demanda visible conserva prioridad y el preload se omite en ese caso. Esta
salvaguarda posterior a la corrida57 se verifica por enlace ARM; no cambia el
caso probado, donde 202 aún no estaba residente.

### Referencia Bob sostenida disponible

`double-baseline55-bob-steady-old`: 2.200 actualizaciones, diálogo y movimiento,
termina sin fallo. Sus últimas ventanas registran P99 de 19,534 a 31,462 ms y
máximo 53,624 ms. GPU previa ~0,68–0,69 ms. `source2DPrepare` llega a 19,625 ms,
pero su alcance incluye esperas del worker/preempciones: NO equivale a tiempo
CPU puro del raster 2D. Hay que separar esa espera antes de atribuir el coste.
La sonda de lectura de imagen y dump al final se excluirá de la comparación
sostenida. Se mantienen todos los registros, incluidos los picos deliberados.

## Investigación del coste sostenido y agrupación — BUILD 58–59

Se verificaron los flags reales del build: Release -O2, ARMv6K, hardfloat/VFP;
no hay un build sin optimización que explique la caída. La fuente oficial
Citro3D1.7.1 confirma que DrawElements actualiza contexto y uniformes por
llamada. BUILD58 muestrea una de cada ocho llamadas, rotando la selección por
frame. En cabeza registra ~18,8 us CPU SDK por llamada frente a ~240 dibujos.
Es tiempo de CPU emulada con sondas, no coste GPU ni medida física de ARM11.

BUILD59 agrupa mallas estáticas compatibles de cada banco al subirlo. No junta
ni reordena dibujos: conserva cada rango, pose, material, luz y visibilidad.
Copia bytes de vértices sin transformarlos y remapea índices a un buffer común.
No crea un segundo buffer temporal CPU del modelo; copia directamente al VBO.
La GPU sigue dibujando los rangos originales. No modifica recursos preparados
ni su identidad/hash de origen.

La admisión de UV fijas se compara para las 64 parejas de dimensiones válidas
8..1024. Sólo se agrupan dominios con idéntica admisión y el mismo formato de
normales; se mantienen los límites de 65.536 vértices y 65.535 índices. Esto
conserva qué materiales dinámicos pueden usar UV enteras exactas. Otros grupos
siguen separados. El mecanismo es general para todos los modelos/canvas.
`SM64DS_NATIVE_MESH_GROUPS=0/1` permite A/B con el mismo binario.

Prueba de datos: 506 bancos, 1.876 mallas → 1.460 grupos. El banco de la cabeza
pasa de 22 mallas a 11 grupos. Para cada vértice e índice se verificó igualdad,
remapeo correcto, normales y equivalencia de admisión UV para las 64 parejas.
También se prueban UV incompatibles, formatos mezclados, techo de índices y
límites de escritura. Evidencia: `mesh-groups-asset-validation.json`.
53 pruebas host y compilación/enlace ABI de BUILD59 pasan. Ganancia de tiempo
pendiente: el conteo de grupos no constituye cumplimiento del objetivo2×.

### A/B de buffers y decisión

BUILD59, mismo binario/opciones/guion salvo `SM64DS_NATIVE_MESH_GROUPS`:
la captura840 es idéntica en ambas pantallas, sin un solo píxel diferente.
Cambios de malla 31→24, pero coste de render 9,72→9,75 ms en esa ventana.
No hay ganancia temporal demostrada. La agrupación queda opt-in, desactivada
por defecto desde BUILD60; no se presenta como avance hacia2× por su conteo.
Los 506 bancos conservan las pruebas de remapeo/UV para trabajo posterior.
`groups59-comparison.json` registra datos y límites.

El censo de tramos muestra que una paleta simple de traslación compartida no
sirve para la mayoría de la cabeza: en una muestra129 partes sólo10 pares
adyacentes conservan los coeficientes lineales y1 conserva todo el estado.
En otra pose hay29 y19, respectivamente. No se implementa una agrupación que
aproxime sus poses/luces para obtener menos llamadas.

### BUILD61: omitir envíos estáticos que no pueden producir píxeles

El cocinador ya separa triángulos de varios huesos para animación CPU y deja
triángulos degenerados como marcadores de material/pose. El banco de la cabeza
tiene22 partes cuyo rango sólo contiene triángulos con índices repetidos;
el catálogo completo tiene223 de estas partes sobre4.458. Antes llegaban a
DrawElements aunque no podían cubrir un píxel.

Se mantiene cada PreparedPart, sus matrices/material/luces y el skin que la
referencia. Sólo se omite la validación/envío de su rango estático vacío en
NativeModel. No se usa culling por posición ni tolerancias; triángulos con
índices distintos se conservan incluso si parecen collineales. Los sprites y
spans de composición conservan su API original. La máscara se prepara al cargar,
no se vuelve a examinar geometría por cuadro. El estado se transfiere/restablece
con el ciclo de vida del recurso.

53 pruebas host y enlace ARM61 pasan; se prueba explícitamente la diferencia
entre índice repetido y vértices de igual posición con índices diferentes.
Auditoría de todos los bancos: `degenerate-parts-audit.json`. Verificación de
imagen y medición de coste sostenido en curso; 2× no está demostrado.

### Resultado inicial BUILD61 en cabeza

1.200 actualizaciones, sin rechazos. La captura840 coincide con BUILD59 salvo
33 píxeles de la etiqueta BUILD; superior idéntica y cabeza/fondo/botones
idénticos. En la ventana correspondiente, llamadas240→218, matrices225→197,
filas de luz1.814→1.505. Render CPU con perfil fino9,72→8,99 ms; activo externo
10,075→9,359 ms. Son una corrida por variante en Azahar, no mediciones físicas.
Los intervalos máximos104/134 ms de esa ventana están contaminados por la
captura de imagen840; se conservan y no se usan para afirmar estabilidad.
P95/P99:16,912/18,074 vs16,946/18,062 ms. No equivale a2×. Evidencia:
`empty61-head-validation.json`. La validación Bob sigue pendiente.

Siguiente cambio de arquitectura a estudiar: un stream nativo de vértices con
matriz de posición y base normal como atributos por instancia, para que varios
tramos de igual material/luz puedan compartir DrawElements sin aproximar poses.
La paleta pequeña por uniformes quedó descartada por el censo de diferencias.
La alternativa debe probar precisión PICA, máximo de atributos, ancho de banda
y tiempo CPU de preparación antes de habilitarse. Las bases normales fuera del
rango representable requieren la ruta exacta existente, sin reducción visual.
También se debe atribuir la interning repetida de luces dentro del productor;
no asumir que toda la captura3,6 ms es cálculo de matrices.

## Corrección del guion Bob — 2026-10-30

La implementación `load_script` usa `%x` para máscaras. Las sondas anteriores
usaban `64/16/65/770` como si fueran decimales: eran combinaciones de botones
no previstas. También `SCRIPT` y `SHOTS` se miden en comienzos de render/base,
no en todas las actualizaciones del límite de juego: 2.200 actualizaciones
terminaban con1.477 capturas base. La foto2.120 y dump2.150 no se ejecutaron.
No hubo borrado de dumps al no alcanzarse la pulsación mal codificada770.

Por ello las carpetas `*bob-steady-*` y `empty61-bob-*` anteriores NO prueban el
recorrido libre completo anunciado. Conservan datos útiles de menú, entrada,
diálogo y parte del juego, etiquetados como evidencia parcial. No se usará su
media como aceptación del objetivo. Las comparaciones de cabeza con toque150
y captura840 no dependen de este error y mantienen su alcance.

Nueva referencia reproducible, siempre con el mismo guion por binario/modelo:
3.000 actualizaciones de límite; A900/1000/1100/1200; UP(40hex)1300..1459;
RIGHT(10hex)1480..1639; UP+A(41hex)1660..1759; DOWN(80hex)1780..1829;
dump parcial L+R+B(302hex)1850. Sin capturas separadas en ventanas temporizadas.
Verificar logs de diálogo cerrado antes de mover y archivos reales de captura.
La pausa del dump al final se conserva y se excluye de percentiles de movimiento.

### Recorrido corregido completado: BUILD55 frente a61

Cuatro ejecuciones secuenciales: `double55-bob-verified-{old,new}` y
`empty61-bob-verified-{old,new}`. Cada una termina3.000 actualizaciones,
1.877 capturas base y2.997 presentaciones sin rechazos. Los cuatro pulsos A
cierran el diálogo; los botones de movimiento son40/10/41/80 hexadecimales.
Dump parcial302 en frame1850, después de las ventanas temporizadas. La imagen
confirma movimiento en Bob-omb; las ventanas ya no son sólo el diálogo.

| Perfil Azahar / fase | Render activo55→61 (ms medios) | Intervalo61 P99 / máximo (ms) |
| --- | --- | --- |
| Old / adelante | 8,333→8,042 | 31,589 /49,654 |
| Old / derecha | 8,870→8,661 | 32,186 /58,346 |
| Old / salto | 7,693→7,470 | 26,829 /27,954 |
| Old / atrás | 6,312→6,083 | 22,358 /22,419 |
| New / adelante | 8,123→7,912 | 24,667 /56,476 |
| New / derecha | 8,623→8,466 | 23,187 /59,988 |
| New / salto | 7,489→7,277 | 22,169 /22,293 |
| New / atrás | 6,114→5,898 | 21,185 /21,375 |

Se incluyen los picos por carga dentro del recorrido. Una ejecución por
variante: no demuestra estabilidad60 ni duplica rendimiento. Las sondas son
costes de pared emulados, no ciclos ARM11 físicos. No sumar alcances anidados.
Evidencia con media/P95/P99/máximo, hashes y errores:
`port-research-20261029/bob61-old-verified.json` y `bob61-new-verified.json`.
El comparador mantenido es `tools/native_renderer/compare_native_timelines.py`.

### CIA61 privada de diagnóstico

Paquete en `playable-r1/hardware/build61`, comprobado con el verificador de
política Old80/New124 MiB. La CIA instalada arranca, presenta la estrella,
revela la cabeza y llega al selector con la imagen superior visible en una
única instancia Old de Azahar (`cia61-installed-old`). Se cerró antes de
compilar62. Esto no demuestra corrección ni rendimiento en consola física.
No es una entrega aceptada como2× y no se publica ningún activo propietario.

### BUILD62: reutilización de conjuntos de luces por contenido

Candidato en evaluación. La captura mantenía un solo conjunto de luces por
valor pero volvía a construirlo y calcular su hash por cada parte/hueso.
Se añade una pista acotada por máscara y modo de tabla/toon que apunta a los
valores ya poseídos por el cuadro. Siempre compara el contenido utilizado;
puntero y generación no prueban identidad. Sólo un fallo de comparación
construye y busca el conjunto canónico. No cambia bases, iluminación ni orden.
Reset/rollback invalidan las pistas. Capacidad y errores conservan su contrato.
53 pruebas pasan, incluidos cambios de fuente sin generación, máscaras/tablas
y rollback/reutilización de slots. Ganancia temporal aún por medir.

### Medición BUILD62

En cabeza Old/Azahar la captura `models`3,63→3,20 ms; el render sostenido
se conserva (~8,6–9,0 ms en las ventanas de igual pose). Superior idéntica y
22 píxeles de etiqueta BUILD diferentes abajo, sin otros cambios. 1.200
actualizaciones sin rechazos. `light62-head-validation.json` conserva hashes.
No se redujo el pico de revelado:553 ms todavía en la ventana605. Allí se
leen bancos222 y4101, aunque el modelo202 ya estaba precargado.

Bob Old/New con el guion corregido completa3.000 actualizaciones por variante,
sin rechazos. Frente55 el render se mantiene en la mejora pequeña de61; los
costes de actualización también tienen esperas y no son CPU pura. Persisten
intervalos de hasta59,128 ms en Old y55,583 ms en New (carga incluida).
Evidencia: `bob62-old-verified.json` y `bob62-new-verified.json`. No cumple2×.

### BUILD63: precarga del conjunto visual del menú

Además de202 se encolan las imágenes del modelo222 y del banco2D4101 al
arranque. No se instancia lógica ni se adelantan animaciones. La extensión de
bancos de modelo ahora conserva texturas ya residentes, como hacía NativeImages:
sólo crea las que faltan y las publica juntas al terminar. Fallos especulativos
mantienen intactas las imágenes visibles, con los mismos límites/LRU y barrera.
El banco original se valida antes de usar sus offsets y su identidad se conserva
por valor. `SM64DS_NATIVE_PRELOAD_MENU=0` permite desactivar sólo estas dos
precargas. El mecanismo no está limitado a un nivel; las identidades aquí son
la previsión conocida del menú, no una excepción en el dibujado de modelos.

GPU lab nuevo: ampliación exitosa conserva el handle anterior, reutilizar no
asigna más memoria, fuente incompatible se rechaza y falta de presupuesto en
la segunda imagen preserva la primera y sus bytes. También se prueba mover
el dueño. `residency63-current-lab-old` completa1.200 cuadros yPASS en Azahar.
La primera carpeta `residency63-lab-old` contenía un3DSX antiguo (sólo se había
reconstruido el ELF), se cerró y NO es evidencia de estas pruebas.
La compilación ARM estricta detectó además tres indentaciones ambiguas del
cambio61: se corrigió sólo su formato. El comportamiento63 no cambia por eso.

### Resultado inicial de precarga ampliada63

Old/Azahar, comparación62→63 con el mismo guion y activos. En la ventana de
revelado601..720, el trabajo máximo553,369→10,008 ms y el intervalo máximo
561,172→18,161 ms. Los bancos222/4101 se cargan en serial1 y no reaparecen en
serial605/609. La imagen840 superior es idéntica y abajo sólo cambian16
píxeles (x46..51,y231..238), correspondientes al número BUILD.

Tradeoff explícito: el diagnóstico de arranque registra un cuadro largo de
1.113 ms antes de presentar el primer cuadro, más378 ms de otros recursos al
inicio; no desaparece el trabajo de carga, se mueve a la fase inicial oculta.
La precarga adicional reserva ~2 MiB GPU (29 imágenes modelo222 y banco4101);
no se precarga toda la ROM. 1.200 actualizaciones sin rechazos, fallos de banco
ni recargas de la cabeza. No contar este pico reducido como2× sostenido.
Evidencia: `preload63-head-validation.json`. New y comparación de igual binario
con `SM64DS_NATIVE_PRELOAD_MENU=0` en curso; hardware sin comprobar.

A/B63 de igual binario completado: sólo cambia
`SM64DS_NATIVE_PRELOAD_MENU=0`. Desactivada: máximo de trabajo569,642 ms e
intervalo577,338 ms en revelado. Activada:10,008/18,161 ms. Captura840 idéntica
píxel por píxel en ambas pantallas, no sólo tolerancia visual. Ambas ejecuciones
terminan1.200 actualizaciones sin rechazos. Esto atribuye la reducción del pico
a la precarga adicional, no a diferencias de otra versión. New con precarga
termina con9,700 ms trabajo máximo/18,223 ms intervalo máximo en revelado;
cabeza840 idéntica aOld. `preload63-head-validation.json` contiene el A/B.

### Regresión Bob63 cerrada en emulación

Old/New secuenciales:3.000 actualizaciones,1.877 capturas base,2.997
presentaciones por ejecución, cero rechazos y cero fallos de carga. El dump
parcial se ejecutó en1850/escena2; ninguna captura deliberada cae dentro de las
cuatro ventanas comparadas. Contra55, abajo es idéntico y arriba sólo difieren
281 píxeles en el contador FPS (x293..342,y26..40), con la misma escena deBob.

| Perfil / fase | Render activo63 medio (ms) | Intervalo63 P99 / máximo (ms) |
| --- | --- | --- |
| Old / adelante | 8,103 | 29,593 /54,743 |
| Old / derecha | 8,626 | 30,991 /60,181 |
| Old / salto | 7,480 | 26,712 /27,771 |
| Old / atrás | 6,098 | 19,644 /19,672 |
| New / adelante | 7,896 | 20,328 /56,426 |
| New / derecha | 8,456 | 24,462 /56,528 |
| New / salto | 7,273 | 19,650 /19,668 |
| New / atrás | 5,891 | 19,008 /19,008 |

Media/P95/P99/máximo de todas las fases y sus alcances permanecen en
`bob63-old-verified.json` y `bob63-new-verified.json`. Persisten picos por carga
dentro del recorrido. No hay prueba de60 estables ni2× sostenido en todo el
juego. La referencia55 no se mueve. Sigue pendiente medir en consola física y
la arquitectura de menor emisión CPU/GPU, además de productores2D de gameplay.

### Entrega privada CIA63

SHA-256 CIA:
`2ae7b574b5e54f4a6a92b0d76ec889e9a08e370ac793de1d85c1ed1ed30f740e`
(52.999.104 bytes). Verificador de política aprobado. La CIA instalada en
`cia63-installed-old` arranca, presenta estrella/título/cabeza y entra al selector
con imagen de castillo arriba; también confirma las tres precargas en serial1.
Se cerró la única instancia de Azahar al terminar. No es validación física.

Entrega LAN privada, sólo el fichero CIA, servidor40812 en192.168.86.215:8787.
QR decodificado con Vision y descarga completa con hash coincidente. Recibo en
`hardware/build63/delivery/receipt.json`. No se publicó ningún recurso en línea.
El siguiente número libre es64. No se marca el objetivo2× como completado.

## BUILD64–65: emisión nativa de uniformes

BUILD64 se reservó pero no compiló: el C original del SDK permite convertir
DVLE_type a GPU_SHADER_TYPE implícitamente; la adaptaciónC++ ahora lo hace de
forma explícita. El número no se reutiliza. BUILD65 compila y enlaza.

Inspección de Citro3D1.7.1: DrawElements llama UpdateUniforms para vertex y
geometry en cada dibujo. Ambas rutas recorren96 flags float aun cuando no hay
shader de geometría. La sustitución de uniforms.c conserva las variables
públicas, constantes, int/bool, ever-dirty y restauración exactas, pero busca
bloques de16/4 flags limpios y tramos contiguos de4 flags sucios mediante
lecturas memcpy alineadas. No usa NEON, no modifica valores ni cambia comandos.
El modo0 mantiene el bucle escalar original dentro del mismo binario;
`SM64DS_NATIVE_UNIFORM_RANGES=0/1` permiteA/B. Configuración sólo al init.

Se conserva el original oficial con licencia en
`third_party/citro3d-uniforms-1.7.1`. La prueba compila ese C sin modificar su
algoritmo, únicamente renombra símbolos para coexistir con el candidato. Cada
modo compara431.792 actualizaciones: todos los patrones16-bit en seis offsets,
mutaciones aleatorias de valores, cambios de shader/constantes, int/bool,
geometry sin programa y Dirty/Clear/restauración. Compara cada palabra emitida
y todos los valores/flags públicos después de cada operación.55 pruebas host
pasan; ambos modos también pasanASan/UBSan. La adaptación sólo toma prestados
tipos públicos del SDK; no adivina el layout del contexto privado.

La propia definición de UpdateUniforms del candidato está enlazada en elELF65;
no se cambió el SDK instalado. El preparador general ya incluye archivosCPP
nuevos; en el checkout generado se verificaron todos los hashes antes de añadir
la unidad alCMake y actualizar su recibo. Hashes de fuente/oracle/libcitro3d y
alcances en `uniform65-build-validation.json`. Ganancia temporal y aceptación
GPU todavía en evaluación. El objetivo2× permanece abierto.

### BUILD66: paquetes de dibujo nativos

La ruta triangulada del SDK escribe15 comandos fijos (30 palabras) por dibujo.
El backend ahora prepara ese paquete una vez por dueño y sólo cambia el rango
al enviar. Cada malla conserva su configuración física de índices al crearla,
después de validar/poseer la asignación lineal. La ruta conserva la misma
UpdateContext, orden de escrituras, reinicio, activación/desactivación de etapa
y borrado de caché de vértices. Se sigue marcando DrawUsed en el mismo flag del
contexto1.7.1 cuya representación usa la unión de compatibilidad ya existente.
El offset0x20 y todos los IDs de registros se comprueban al compilar ARM.
No se cambian las matrices, vértices, shaders, dibujos ni cadencia.

`SM64DS_NATIVE_DRAW_PACKETS=0/1` selecciona construcción SDK o paquete, de forma
independiente a uniformes por bloques. El margen existente de8.192 palabras se
comprueba antes de actualizar contexto; el paquete sólo ocupa30. Se usa la API
oficial AddRawCommands (copia por palabras), sin escribir fuera del buffer SDK.

El C oficial1.7.1 se compila como oracle independiente y genera100.000 rangos
con offsets, valores y conteos diferentes. Las30 palabras coinciden en cada
caso, incluidas las escrituras duplicadas del SDK. La sanitización detectó el
shift firmado original `type<<31`; sólo esa comprobación shift-base del oracle
upstream se omite para conservar su texto. El candidato usa constantes y
aritmética unsigned y mantiene toda su sanitización.56 pruebas pasan y tres
pruebas nuevas de emisión pasan ASan/UBSan. No se usa ese conteo como prueba2×.
Los núcleos y la lógica del juego no cambian. Ganancia temporal aún por medir.

### Experimento66 y corrección del selector

Las imágenes840 de ambos modos66 coinciden píxel por píxel. En la ventana
900..1100 (sin foto840 ni dump1180 dentro), el render activo medio10,410→7,134 ms,
pero el modo escalar66 paga un selector repetido por flag. NO se usará esa
referencia más lenta como baseline del objetivo. La referencia55 queda intacta.
Contra el stock63, emisión de cabeza~19,2→9,7 us por dibujo y render~8,6–9→6,8–7 ms:
ganancia parcial, no2× del cuadro. Evidencia `emission66-head-experiment.json`.

BUILD67 especializa ambas búsquedas en código separado: el selector de modo
se evalúa una vez por llamada, fuera del bucle. Ambos siguen verificados contra
el C oficial. La etiqueta de muestra pasa a GPU emission: incluye contextoSDK
más el emisor elegido, no necesariamente DrawElements original. No se atribuye
esa medida de CPU a tiempo GPU. Se medirá de nuevo antes de aceptación.

### Resultado sostenido67: cabeza Old/emulada

Se repite el binario55 congelado con perfil fino1 y el mismo guion, foto840 y
dump1180. La ventana900..1100 excluye ambas lecturas deliberadas.200 muestras
por variante: render activo medio9,692→7,140 ms (26,3% menos), P99
10,340→7,760 ms y máximo10,674→8,012 ms. Intervalo67 medio16,718 ms,
P99 18,064 y máximo18,167 ms. Imagen840 superior idéntica; abajo sólo33
píxeles del númeroBUILD(x40..51,y231..238). Sin rechazos/fallos de carga.
El muestreo de emisión67 queda~9,7–9,9 us por dibujo, sin confundirlo conGPU.

No se alcanza la reducción50% del coste activo del cuadro. La actualización
original/captura/skin y el resto del render siguen costando tiempo. La ganancia
no procede de bajar resolución, saltar dibujos o alterar animaciones. La
comparación0/0↔1/1 de67 usa el mismo binario y confirma el efecto de los dos
emisores, pero el objetivo se compara contra55, no contra esa ruta escalar.
`emission67-head-old-verified.json` conserva todas las distribuciones y hashes.

New/emulada en la misma ventana:55→67 render activo9,388→6,794 ms (27,6%
menos), P99 10,047→7,457 y máximo10,346→7,668 ms. Intervalo67 P99
17,990 ms/máximo18,121. Igualdad de imagen salvo33 píxeles del númeroBUILD.
Evidencia `emission67-head-new-verified.json`. La comparación de igual binario
67 Old escalar/nativo da9,364→7,140 ms y ambas imágenes idénticas;
`emission67-head-ab-old.json`. El beneficio del objetivo sigue calculándose
contra55, no contra ese modo de control. Ningún resultado es físico.


### Bob-omb67 y alcance de precarga

Old/New secuenciales, mismo guion hexadecimal y ventanas que55:3.000
actualizaciones,1.877 capturas base,2.997 presentaciones, sin rechazos ni fallos
de carga. Los dumps1850 quedan fuera de las ventanas de tiempo. Old conserva
ambas pantallas salvo281 píxeles del contadorFPS(x293..343,y26..40); New
coincide en ambas pantallas. Comparaciones e identidades deCSV en
`emission67-bob-{old,new}-verified.json`; verificación visual en
`emission67-bob-image-validation.json`.

| Perfil / fase | Render55→67 medio(ms) | Intervalo67 P99 / máximo(ms) |
| --- | --- | --- |
| Old / adelante | 8,333→6,663 | 28,415 /49,974 |
| Old / derecha | 8,870→7,378 | 31,296 /60,785 |
| Old / salto | 7,693→6,315 | 26,568 /27,864 |
| Old / atrás | 6,312→5,015 | 25,186 /25,287 |
| New / adelante | 8,123→6,481 | 25,214 /48,259 |
| New / derecha | 8,623→7,203 | 23,958 /59,745 |
| New / salto | 7,489→6,152 | 19,650 /19,678 |
| New / atrás | 6,114→4,855 | 19,008 /19,654 |

Reducción del render activo~17–21%, no50%; persisten picos de carga y
presentación. No equivale a60FPS estables ni al objetivo2×. source2DPrepare
incluye espera/preempción; no debe sumarse como CPU independiente ni llamarse
tiempo puro del rasterizador. Una ejecución por variante, sólo emulación.

La revisión solicitada por el propietario confirma precarga activada para
modelo202, imágenes222 y banco4101 enserial1. El A/B63 ya demuestra el pico
569,642→10,008ms (trabajo) al revelarse, a cambio de trabajo inicial adelantado
y~2MiB GPU adicionales. No se encontró una nueva carga de esos bancos durante
la cabeza que justifique otra caché. No se declara precarga de animaciones CPU:
esa ruta todavía requiere medición específica. El render sostenido continúa
procesando matrices, skin y envíos por cuadro. La precarga existente no prueba
que el problema reportado en Old física esté resuelto.


CIA67 privada:53.003.200 bytes, SHA256
`fbd6c898577e38dfe1c433148ce30f6b41beb6c5a4b1f4477507dc12cfaef6bf`.
Verificación de política/título/ELF en `hardware/build67/integrity.json` y
recibo de empaquetado. Instalada con el lanzador protegido Old/Azahar, se
observan estrella inicial, logo/cabeza y Aventura→selector con fondo del
castillo arriba. No se midió rendimiento físico. QR decodificado con Vision
y descarga LAN cotejada por tamaño/SHA; recibo en `hardware/build67/delivery`.
El servidor existente sigue accesible y sólo sirve CIA63/CIA67; no se publica
ninguna ROM, save ni dump. Azahar de prueba detenido y salida comprobada.


### BUILD68: matrices del skin por captura

El skin medido en cabeza67 conserva~1,32ms dentro del coste de captura de
modelos~3,2ms; no se suman ambas cifras. Se reutilizan las conversiones de
48bytes enteros→12float y las inversas por hueso durante una captura. Se
mantienen el orden de productos/sumas, sqrt, redondeo de normales, fallback de
hueso y descarte de grupos singulares. No se cachean posiciones animadas entre
cuadros ni entre actores. La caché pertenece al workspace del hilo de juego;
se reinician sus flags antes de cada modelo. Coste fijo adicional24.832bytes,
sin asignación de heap por captura. Las matrices no usadas no se convierten.

Las56 pruebas host pasan. Se amplía la prueba de captura con dos actores que
comparten catálogo y tienen poses distintas, dos grupos repetidos, origen255,
objetivos distintos de posición/normal, escala no uniforme y giro, propiedad
del snapshot publicado, cambio de pose entre cuadros, matriz singular y su
recuperación. La prueba también pasaASan/UBSan. Compilación ARM68 correcta.
No se atribuye todavía ganancia temporal: comparación en curso con67 y55.
`skin68-source-validation.json` conserva hashes y alcance de comprobación.


Resultado68 en cabeza: Old67→68 skin1,32→0,92ms y captura3,20→2,80ms;
New~3,20→2,82–2,86ms, mismo skin0,92ms. Las capturas840 coinciden salvo27
píxeles del númeroBUILD. El render activo sigue7,140ms Old/6,795ms New:
no disminuye frente a67. Es una reducción del trabajo de captura, no2× ni una
mejora de FPS sostenida del cuello de render. `skin68-head-*-vs67.json`.

Bob68 Old/New:3.000 actualizaciones/1.877 capturas/2.997 presentaciones, sin
rechazos. Contra67 ambas imágenes iguales salvo contadorFPS(90píxeles Old,
0New), sin cambios de geometría. En las cuatro fases, render68Old
6,727/7,333/6,351/5,057ms; New6,462/7,199/6,152/4,862ms. Contra55 sigue~17–21%
menos. Intervalos máximos durante avance/derecha58,712/51,071ms Old y
51,539/55,888ms New. Las fases de salto/atrás quedan bajo19,7ms en estas
muestras; no se afirma ausencia de picos general. El último agregado120base
muestra skin0,29ms en ambos perfiles; ámbitos log no sustituyen CSV por fase.
`skin68-bob-*-vs{55,67}.json` e imágenes en `skin68-bob-image-validation.json`.

### BUILD69: captura completa para reducir envíos

Se encontró una carencia del diagnóstico: SCENE4 rechazaba cualquier
Occurrence::skin/mesh. Por eso ciertos JSON estaban vacíos aunque CSV/RGB
fueran válidos. SCENE5 valida y exporta el arena de NormalVertex, rangos
ordenados de skin/mesh, highlight, toon/grey y paleta toon. La herramienta de
fixture conserva también rangos de quads por capa de pantalla que antes se
omitían. No se cambia el dibujo ni el coste habitual: sólo solicitudes de
captura ejecutan esta exportación invasiva, fuera de ventanas temporizadas.
El C++ generado se compila y vuelve a exportar JSON: todos los campos coinciden,
incluidos los nuevos. Las tres pruebas de captura/fixture/roundtrip pasan
ASan/UBSan. Un fallo inicial de dependencia compartida del generador CMake se
corrigió con un único target dueño de ambas salidas; build posterior correcto.

El censo offline usa los validadores reales de captura y ModelBank. Sólo cuenta
tramos adyacentes, omite exactamente los marcadores degenerados e introduce
barreras de quads/material/textura. Diferencia una base normal por atributo de
preiluminación con paleta16. La prueba controlada de seis dibujos y una barrera
identifica correctamente cuatro/dos envíos potenciales, y conserva un cambio
de material como barrera. Esto es oportunidad estática; todavía no es una
implementación de batching ni una medición de ganancia temporal.


Capturas reales:69 cabezaSCENE5 válido194.704bytes,131 partes/93 ocurrencias/
1.038 vértices de skin;70Bob88.073bytes,120partes/19ocurrencias/330vértices.
Cabeza69vs68 foto840 igual salvo5 píxelesBUILD; Bob70vs68 dump1850 igual salvo
90píxelesFPS arriba e idéntico abajo. El replay inicial encontró otra omisión
histórica: capas solid y tone_target no estaban cubiertas por el fixture.70
conserva esos campos y corrige el puntero/contador de skin del constructor del
snapshot de la opción COMPONENT_CAPTURE. No se relaja la negativa de captura
original incompleta: el diagnóstico sigue marcado originalSceneAccepted=false.

58 pruebas host pasan (este build host tiene ASan/UBSan activados, según cache).
El probe offline debe enlazarse con --sanitize contra esa biblioteca; un enlace
sin ese flag falló y se repitió correctamente tras comprobar el cache, sin
cambiar código de producción. El probe valida snapshot y cada ModelBank con
los validadores reales antes de contar geometría. Cabeza149→34 envíos de
geometría, conservando51 barrerasquads; Bob102→34 atributos /33prelitpaleta16.
Ambos censos conservan hashes de captura/bancos y límites. Aún NO hay un shader
batch ni una medida de reducción de tiempo por esta propuesta. El próximo
trabajo debe implementarlo y comprobar precisión/capacidad/propiedad de frame,
en vez de atribuir esa reducción de llamadas aFPS ya logrados.


CIA70:53.003.200bytes, SHA256
`b86df523af3bd04cbb4405a152b93a123203ed5ac7da8e3143635093ed5d41cf`.
Paquete verificado e instalado con perfilOld deAzahar: estrella inicial,
logo/cabeza animada y Aventura→selector con fondo superior visibles. No es una
prueba física ni una aceptación2×. Entrega LAN en el servidor existente, con
hash/tamaño de descarga verificados; los únicos ficheros servidos sonCIA63,
CIA67 yCIA70. Azahar de prueba detenido. Recibos en `hardware/build70`.


### BUILD71–72: agrupación nativa con color entero y paleta16

Se implementa color CPU exacto con entradas semánticas ya validadas:8.192 casos
comparan contra el oráculo entero de ancho amplio y1.024 contra la ejecución
f24 del shader real. Incluye coeficientes32bit, signed11, residuos20bit,
especular, shininess, toon y grey; salida RGB5 expandida aRGBA8. La nueva entrada
PICA conserva posición/UV/tint:4.096 ejecuciones f24 comparan los16 selectores
con el shader corto anterior. Las posiciones no se transforman enCPU.

El plan es dueño de vértices28bytes/indexado/colores por presentación; no edita
VBO compartidos ni conserva punteros del juego. Un lote contiene hasta16MVP
completas y reutiliza matrices idénticas por valor. Sólo agrupa adyacentes con
el mismo material/textura, manteniendo barreras de quads y ruta anterior para
normal/fixedUV y highlights. Límites8.192vértices/32.766índices/256lotes; un fallo
de preparación conserva la ruta anterior completa para ese cuadro. La revisión
del plan se liga a la subida GPU, y la barreraGPU protege su reescritura.

El shader enlazado reutiliza c0..63 para matrices y c64..66 paraUV/tint; init
comprueba ubicación y no solapamiento de constantes. Cambiar de entrada invalida
las cachés compartidas, incluidas luces/tablas del shader general. No se
modifica el SDK instalado ni se añade un decoder de comandos. Modelo completo
se valida antes de emitir partes, conservando el rechazo transaccional.

71 queda reservado como compilación fallida: uint32_t esunsignedlong en el SDK
ARM yunsignedint en host; min ahora especifica el tipo sin cambiar aritmética.
72compila. Se corrigió el generador del lab de luces: tras añadir la paleta toon
faltaba su campo en el agregado, desplazando known/table; ahora es explícito.
El primer lab72 agotó su pila al alojar~50KiB de casos enmain antes del log;
los datos del lab pasaron a estáticos. Esa ejecución NO cuenta como validación.

LabGPU corregidoOld/Azahar:128casos,120cuadros, capturas referencia/nativa/última
idénticas.70colores reales en la imagen, no salida vacía. Comprueba16matrices,
restauración de iluminación/tabla y corto afín, negativa de subir mientrashay
dibujos pendientes y memoria residente constante. Evidencia `batch72-gpu-validation.json`.
Las60pruebas host previas al nuevo test f24 pasan; el test de4.096 casos pasa
por separado. Comparación del juego aún en curso; ninguna aceptación2×/física.


A/B72 de cabeza Old con el mismo binario: ventana900..1100 render activo
6,985→10,091ms, regresión. Ambas capturas840 son idénticas píxel a píxel.
Los envíos bajan (146dibujos afines→28lotes tras reutilizarMVP iguales), y
submit~4,1→1,8ms, pero preparar el plan consume~5,74ms.72 NO se acepta como
mejora ni se empaqueta para reemplazar70. `batch72-head-regression.json`.

73 prepara coeficientes luz×material y ambiente una vez por estado, con copia
propia de tablas. El transformado normal usa sólo productos unsigned32: sólo
bits12..22 sobreviven al shift12/signed11, por lo que conserva exactamente el
oráculo wide-integer sin multiplicaciones64 innecesarias. Recíproco y cuadrado
especular tienen producto acotado a268.173.312, válido en signed32. La caché de
normales usa clave30bit y mezcla avalancha, comparación de dos enteros en vez
de array/memcmp, y32sondeos máximos con cálculo exacto sin caché al excederlos.
Un caché directo de64estados posee coeficientes/tablas y se reinicia por frame;
los IDs pertenecen a la paleta inmutable de esa presentación.8.192casos/1.024
f24 siguen coincidiendo, además del plan y4.096casos del shader corto. Medición73 Old:9,242ms de render activo medio, p99 9,994ms, máximo10,163ms,
frente6,985ms del control72. La preparación queda en~4,9ms; aún regresa. No se
acepta ni entrega. Evidencia `batch73-head-old-vs72-control.json`.

### BUILD74–75: atributos nativos, iluminación GPU, experimento optativo

Se sustituye el color CPU por normal/base signed16 como atributos. Paleta de
8MVP,60bytes/vértice, luces/tablas poseídas por lote, sin alterar orden ni
transformar posiciones en CPU. La ruta normal es la predeterminada; la nueva
requiere `SM64DS_NATIVE_BATCHES=1` hasta demostrar mejor tiempo total.

61pruebas host pasan. El lab74 detectó solapamiento de constantes entre DVLE;
una sonda GPU confirmó normales iguales y colores distintos. Se igualan las
12declaraciones/IDs/datos de constantes del helper compartido y se valida su
ABI al inicializar. El primer lab75 llevaba una cabecera previa restaurada por
la sonda; el backend la rechazó correctamente.75b regenera el shader mantenido.
Detalle y criterios en `NATIVE_ATTRIBUTE_BATCH_PROTOTYPE.md`.


Lab75b GPU:120cuadros, referencia/lotes/repetición idénticos(288.000bytes por
captura); verificación binaria de12constantes coincidente y61pruebas host.
A/B75 cabeza Old de igual binario:render activo medio6,993→7,122ms;
p99 7,612→7,833ms, máximo7,824→8,092ms. Intervalo máximo18,087→18,149ms.
Captura840 idéntica en ambas pantallas;1.200actualizaciones/ruta, sin rechazos.
Preparación~2,6ms,submit~1,9ms,upload~0,77ms. Se mantiene optativo y NO se
empaqueta como mejora. No se amplía aNew/Bob una ruta que aún regresa en cabeza.
`batch75-head-same-binary-old.json`, `batch75-gpu-validation.json`.
La siguiente reducción debe evitar reconstruir/copiar topología inmutable cada
presentación, con precálculo residente acotado durante carga y ownership real.
La precarga existente de modelo202/imágenes222/4101 permanece predeterminada;
no se repiten nuevas cargas durante la ventana de cabeza. Objetivo2× y prueba
física continúan pendientes; la CIA70 entregada no se ha reemplazado.


### BUILD76–78: caché de topología, plantillas y bucles ARM

76 separa preparación por fases en muestras optativas.77 compacta topología
una vez durante carga (CPU hasta1MiB compartido,128KiB transitorios); en cabeza
106partes usan caché y40rangos skin usan sus índices identidad.78 guarda la
plantilla final, copia por bloque y usa ldmia/stmia para renovar atributos y
cada prefijo skin actual. No conserva poses ni modifica activos compartidos.
61pruebas host con sanitizadores pasan; labGPU120cuadros verifica prefijos,
bases/selectores, dos actores/rangos, dos poses distintas y capturas idénticas
con el renderer original. `batch78-gpu-validation.json`.

A/B78 mismo binario Old cabeza900..1100:render activo6,992→6,799ms;
p99 7,613→7,599ms, máximo7,868→7,636ms. Intervalo máximo18,210→18,172ms.
Capturas840 idénticas en ambas pantallas. Old/New78 también coinciden.
Contra55:Old9,692→6,799ms,New9,388→6,456ms (29,8/31,2% menos, no50%).

Bob78 Old frente68:6,727/7,333/6,351/5,057→7,221/7,847/6,892/5,708ms.
New:6,462/7,199/6,152/4,862→7,031/7,656/6,686/5,512ms. Todas las ventanas
regresan (forward/right/jump/down). Frente55 sólo9,6–13,4% menos en estas
ventanas, por debajo de la ruta68. Picos de trabajo activo40,724–47,532ms e
intervalos52,736–57,172ms en movimiento/carga.3.000actualizaciones por perfil,
sin rechazos; no se afirma estabilidad100% ni aceptación física.

La variante78 permanece desactivada por defecto. No se empaqueta como mejora
ni reemplaza70. La campaña guardada respetó una sola instancia; el primer
script se detuvo al observar el PID recién terminado tras SIGKILL, confirmó
salida y continuó sólo los tres escenarios pendientes. Todos los procesos de
prueba están terminados. Las comparaciones están en `batch78-head-*.json` y
`batch78-bob-*.json`. Próxima arquitectura concreta y pruebas en
`NATIVE_RESIDENT_PALETTE_DESIGN.md`: evitar copia/subida intermedia por frame.
El objetivo2× sigue activo, con el criterio55 original y hardware pendiente.


### BUILD79–82: geometría residente, subida dinámica única y CIA de hito

Se sustituyó el stream intermedio60bytes/vértice de78 por grupos rígidos GPU
preparados al cargar y paleta8 de matrices/bases normales. Las plantillas CPU
son transitorias y se liberan tras la carga; skin y quads actualizan una sola
vez su prefijo actual de36bytes. El plan conserva rangos/uniformes, sin copiar
ni volver a subir geometría. Se mantienen los recursos originales para los
shaders generales, toon/tablas/UV/bases no admitidas, sin quitar efectos.
La caché de uniformes81 evita filas idénticas; sus contadores bajan, pero no se
atribuye una mejora temporal aislada a esa caché:80/81 difieren menos de1%.

61pruebas host con ASan/UBSan pasan. LaboratorioGPU120cuadros: ocho selectores,
dos poses actuales, partes ocultas, dos rangos skin y dos quads, fallback/restauración
de tablas/toon, mezcla/tinte/brillo y restauración de luz tras un quad sin luz.
Capturas exactas;504bytes dinámicos por cuadro y cero rígidos. Move/reset,
presupuesto opcional y handles de generación obsoleta comprobados. La primera
fixture79 tenía padding/layout incorrecto y se corrigió antes de estas pruebas;
no se distribuyó una CIA79. Evidencia `resident81-gpu-validation.json`.

A/B81: ocho ejecuciones secuenciales (cabeza/control y nativa, Bob/control y
nativa, Old y luego New), una sola instancia, sin compilación simultánea.
Cada cabeza1.200actualizaciones y Bob3.000;0rechazos. Imágenes del A/B de cabeza
Old y ambas pantallas/escenas Bob de cada perfil coinciden exactamente.
Los rangos de cabeza900..1100 y Bob1340..1460/1480..1640/1660..1760/1780..1830
son los mismos que55. Una ejecución por variante; tiempo wall, fases anidadas
no sumables. Todo lo que sigue es **emulación**, no rendimiento de consola.

| Perfil / ventana | Media render activo55→81(ms) | Reducción | P99 / máximo81(ms) | Intervalo máximo81(ms) |
| --- | --- | --- | --- | --- |
| Old / head | 9.692→5.903 | 39.1% | 6.631 / 6.741 | 18.074 |
| Old / forward | 8.333→5.674 | 31.9% | 9.943 / 38.435 | 49.095 |
| Old / right | 8.870→6.387 | 28.0% | 10.302 / 45.407 | 54.983 |
| Old / jump | 7.693→5.461 | 29.0% | 9.075 / 9.199 | 19.613 |
| Old / down | 6.312→4.303 | 31.8% | 8.999 / 9.102 | 19.613 |
| New / head | 9.388→5.631 | 40.0% | 6.329 / 6.429 | 18.123 |
| New / forward | 8.123→5.461 | 32.8% | 9.798 / 34.714 | 45.652 |
| New / right | 8.623→6.185 | 28.3% | 9.777 / 40.703 | 51.814 |
| New / jump | 7.489→5.272 | 29.6% | 8.943 / 8.964 | 23.381 |
| New / down | 6.114→4.114 | 32.7% | 8.741 / 8.859 | 23.737 |

A/B del mismo binario: cabezaOld7,041→5,903ms yNew6,698→5,631ms; Bob13–16%
menos render activo en las cuatro ventanas. Frente55 la reducción sigue siendo
39–40% en cabeza y28–33% en Bob. No cumple aún>=50% ni estabilidad100%.
Hay cargas durante movimiento (2muestras adelante,3derecha). En Old los intervalos
>25ms del control→nativo cambian9→16 adelante y29→3derecha: no todos los picos
mejoran. Salto/atrás Old no registran cargas ni intervalos>25ms. Los JSON
`resident81-*-vs55.json` y `resident81-*-vssame-binary.json` guardan distribuciones
completas, hashes de CSV y conteos.

BUILD82 activa esta arquitectura por defecto. Verificación1.200actualizaciones
Old sin variable de activación:0rechazos, imagen superior idéntica a81; inferior
sólo cambia22píxeles del número BUILD. La CIA privada de53.015.488bytes contiene
el mismo renderer, recursos RomFS y entrypoint manual perfilado. SHA256:
`075fa7e51114e0cef163db498dd68444b4cd70f48be73b028ee15b75bedfc5f4`.
Código CIA contrastado con ELF, política80MB Old/124MB New verificada. Instalada
en Old Azahar: estrella, título/cabeza y Adventure→selector con fondo superior
visibles; no prueba física. Log confirma precargas202/222/4101 en serial1 y
`timings=1`. Se cerró la única instancia al terminar; QR y descarga desde el
servidor LAN existente verificados byte/hash. Evidencia del paquete:
`playable-r1/hardware/build82/{integrity,installed-validation}.json` y
`delivery/receipt.json`.

Sobre la pregunta de precarga: ya funciona para el conjunto gráfico de cabeza.
Adelanta carga al arranque y evita nuevas lecturas/subidas al revelado; no elimina
el coste por cuadro de lógica, animación, captura y dibujo. No se añade otra caché
ni se anuncia precarga de animación CPU sin medición. El objetivo2× permanece
activo. La ruta2D de juego todavía conserva un puente PPU temporal y no debe
anunciarse como juego100% nativo. Continúan pendientes CPU/2D, picos de carga y
validación Old/New físicas.


### BUILD83–85: frontera tipada de vértices y dominio UV nativo

El muestreo83 separa nueve
etapas en una presentación cada240 (`SM64DS_NATIVE_STAGE_PROFILE=1`, por defecto
apagado). En cabeza serial960: dinámica1,637ms, preflight0,323ms, plan1,002ms,
geometría1,972ms, composición0,348ms y finalización0,240ms; validación0,216ms.
En Bob la etapa preflight también incluye subir los planos del puente2D, por
lo que no se interpreta como validación pura. Muestras de etapa incluyen relojes
y no sustituyen el A/B de tiempos completos.

La subida skin usaba el lector de archivos MeshBytes: reconstruía floats/normales
desde bytes y revisaba los índices identidad residentes en cada pose.84 valida
NormalVertex directamente en su frontera, sin volver a revisar índices creados
por el dueño. Conserva UV, normales y el comportamiento lenient: un prefijo no
válido sigue excluido de la paleta, con dominio vacío como antes. Un puntero
nulo con vértices se rechaza antes de copiar. El cálculo exacto de precisión UV
usa CTZ en lugar de desplazar hasta23veces; la salida ARM observada contiene
RSB/AND/CLZ, sin instrucción de una arquitectura posterior.

85 expone ese cálculo pequeño al inlining y hace las pruebas de finitud IEEE754
por exponente, evitando transferencias VFP→flags repetidas. Los rangos dinámicos
ordenados se comprueban por intervalos antes de construir un mapa por vértice.
Rangos desordenados/solapados siguen por la ruta completa y excluyen ambos actores.
No se conservan poses ni se mueve el coste al hilo de juego.

61pruebas host ASan/UBSan pasan. La frontera tipada coincide con MeshBytes en
4.096casos incluyendo NaN/infinito/normales/modos UV inválidos. El oráculo UV
cubre todos los exponentes finitos, signos y cantidades de bits bajos, incluidos
subnormales y ceros.1.024escenas de rangos se comparan con un oráculo de
intersecciones, tanto ordenadas como desordenadas. Evidencia de host:
`/tmp/sm64-stream85-host-tests-final.log`.

Campaña85, sin el muestreo extra de etapas: cabeza1.200actualizaciones y
Bob3.000, Old y New secuenciales, cero rechazos; sin compilar durante pruebas.
Las cuatro escenas son idénticas a81. Cabeza superior idéntica y sólo26píxeles
inferiores del BUILD cambian (x46..51,y231..238). Bob conserva ambas pantallas y
la captura completa de componentes exactamente. Una corrida por variante en
Azahar; ninguna de estas cifras prueba rendimiento físico.

| Perfil / ventana | Media activa55→85(ms) | Reducción | P99 / máximo85(ms) | Intervalo máximo85(ms) |
| --- | --- | --- | --- | --- |
| Old / head | 9.692→5.507 | 43.2% | 6.198 / 6.290 | 18.066 |
| Old / forward | 8.333→5.576 | 33.1% | 10.158 / 47.980 | 58.915 |
| Old / right | 8.870→6.259 | 29.4% | 9.763 / 46.425 | 57.383 |
| Old / jump | 7.693→5.308 | 31.0% | 9.019 / 9.063 | 21.158 |
| Old / down | 6.312→4.151 | 34.2% | 8.818 / 8.845 | 20.570 |
| New / head | 9.388→5.179 | 44.8% | 5.876 / 5.995 | 18.100 |
| New / forward | 8.123→5.392 | 33.6% | 10.008 / 41.981 | 53.079 |
| New / right | 8.623→6.073 | 29.6% | 9.631 / 45.201 | 55.216 |
| New / jump | 7.489→5.134 | 31.4% | 8.805 / 8.826 | 19.657 |
| New / down | 6.114→3.977 | 35.0% | 8.627 / 8.761 | 19.678 |

Frente al renderer81 que82 activa por defecto, cabeza mejora6,7% Old/8,0% New y
Bob1,3–3,5% según ventana. El pico de carga NO queda resuelto: Old llega a58,915ms
e intervalo New55,216ms, mayores que en81. No se afirma mejora de todos los picos,
60FPS estables ni cumplimiento2×. Faltan~0,66ms de render en cabezaOld y~1,82ms
en la ventana derechaBobOld para llegar a la mitad de55. La ruta2D de juego
mantiene un puente temporal; es parte del trabajo pendiente, junto a las cargas.
Datos completos y hashes: `stream85-*-vs55.json`, `stream85-*-vs81.json` y
`stream85-validation.json` bajo `build/native-renderer/port-research-20261029`.

LabGPU85 completa120cuadros con cinco readbacks idénticos a81 y dos poses
distintas; además rechaza puntero nulo/prefijo inválido de la paleta y vuelve
a aceptar las poses válidas siguientes. `stream85-gpu-validation.json`.
CIA85 empaquetada e integridad ELF verificada:53.015.488bytes, SHA256
`426fc1655dada261750acdee61f7894d74744c964b6ab69e609223018bf9bdac`.
Instalada en Old Azahar: estrella, Mario y Adventure→selector con fondo superior
visibles;0rechazos y tiempos habilitados. Instancia cerrada al terminar.
QR privado y descarga LAN verificados por Vision y SHA256. Recibos en
`playable-r1/hardware/build85/{integrity,installed-validation}.json` y
`delivery/receipt.json`. Prueba física y objetivo2× siguen pendientes.

## BUILD86–88: selectores empaquetados y validación ARM entera

Revisión del 2026-10-30. La investigación ampliada y las fuentes primarias
(Citro3D1.7.1, libctru, OpenLara y SM64) están en
`docs/3DS_PORT_RENDER_RESEARCH.md`. La ruta de juego sigue conservando
`ppu_prepare_candidates_cached`, composición de candidatos y subida de planos
`Bridge2D`; por ello no se declara completada la migración gráfica100% nativa.

86 sustituye el bucle de cuatro bytes por vértice por una palabra empaquetada
de selectores MVP. Se verifica el ensamblado ARM: una escritura32bit alineada,
con el mismo stride36 y sin nuevas instrucciones fuera de ARMv6K. 88 añade
clasificación IEEE754 por registros enteros en `finite(Matrix)` y desenrolla
las cinco comprobaciones de finitud del stream tipado. Se conservan todos los
rechazos, UV, normales, precisión de shader, coherencia y fence. No cambia
simulación, resolución, contenido ni orden de dibujos.

87 no produjo candidato: el acceso `.data()` usado inicialmente sobre una fila
C no compilaba. El comando de pruebas continuó usando binarios anteriores;
esos resultados NO validan87. Se corrigió a `&row[0]`, se reservó88 y se verificó
el éxito del rebuild antes de ejecutar las pruebas nuevas. 61/61 pruebas
ASan/UBSan de88 pasan. El oráculo contrasta todos los exponentes, ambos signos
y cuatro límites de mantisa en cada una de las16 componentes de matriz y las
cinco de vértice, incluyendo ceros, subnormales, NaN e infinitos. La prueba de
selectores cubre los ocho slots y sus cuatro filas.

ARM88 y laboratorio GPU enlazados. Lab Old/Azahar:120cuadros, cinco readbacks,
las parejas referencia/nativo coinciden (FNV8cee6185 y8d67a93d para las dos
poses). Incluye múltiples instancias, estados/materiales y restauración de
programa. No es validación del PICA físico.

Campañas86 y88: cabeza1.200actualizaciones y Bob3.000, primero Old y luego
New, secuenciales, sin compilación durante emulación. Una sola instancia en
todo momento, launcher guardado y salida confirmada antes de cada lanzamiento.
Cero errores de timeline. Cabeza coincide salvo la etiqueta BUILD; Bob coincide
píxel por píxel en ambas pantallas. Las ocho corridas reutilizan los guiones85;
no se cambiaron las ventanas ni se suprimieron las cargas del movimiento.

Resultados88 (media activa / P99 activo / máximo activo / máximo intervalo,
en ms, todos de emulador; fases anidadas no sumables):

- Old cabeza:5,317 /5,970 /6,088 /18,183.
- Old adelante:5,459 /9,668 /46,012 /56,830.
- Old derecha:6,233 /9,708 /48,923 /58,805.
- Old salto:5,235 /8,951 /8,984 /22,029.
- Old atrás:4,082 /8,627 /8,733 /19,817.
- New cabeza:4,986 /5,682 /5,777 /18,097.
- New adelante:5,276 /9,583 /35,250 /48,987.
- New derecha:5,995 /9,551 /45,966 /56,063.
- New salto:5,067 /8,736 /8,757 /27,952.
- New atrás:3,910 /8,537 /8,653 /24,706.

Frente85: cabeza−3,5% Old/−3,7% New; Bob−0,4..2,1% Old y−1,1..2,1% New.
Frente55: cabeza−45,1% Old/−46,9% New; Bob−29,7..35,3% Old y−30,5..36,0% New.
Son una corrida por variante: las pequeñas diferencias de gameplay pueden
incluir variación del host. No todos los percentiles/intervalos mejoran. La
carga de derecha sigue llegando a58,805ms de intervalo Old y salto New llega
a27,952ms. El objetivo>=50% de reducción y la aceptación física siguen abiertos.

Datos y hashes de CSV: `build/native-renderer/port-research-20261029/finite88-*-vs85.json`,
`finite88-*-vs55.json`, `finite88-validation.json`, logs y guiones archivados.
3DSX88 SHA256 `f0788b04ca3926b60f1da3da29b7702058aa14212aca3b4064268521bb9d2645`.
No se empaqueta CIA ni se cambia la referencia55. Todas las instancias de prueba
quedaron cerradas. Pendientes de mayor alcance: atribuir espera vs trabajo de
preparación2D, completar productores2D semánticos, reducir picos de carga y
medir en la Old física donde el propietario observó la regresión.

## Aceptación física88 rechazada; correcciones91

El propietario probó88 en Old: estrella negra, caída60→30 en menú y Adventure
bloqueado en blanco con música cortada. El registro físico confirma audio sin
worker y termina en la primera carga4111. La investigación y las correcciones
están en `NATIVE_RENDERER_BUGS_2026-10-27.md`, sección «Reporte físico Old».
La siguiente CIA91 restaura sprites afines identidad, corrige cuota/fallback
de audio y evita releer un banco completo al añadir imágenes. Desactiva las
sondas finas nativas por defecto para jugar; eso no se contabiliza como avance2×.
Se conserva el pendiente de rendimiento físico sostenido y la validación de
Adventure en la consola del propietario. Ninguna media de Azahar cierra ese
pendiente. La imagen negra de estrella sí se reprodujo en un instante omitido
por los guiones de cabeza anteriores y se contrastó88→91 en ese mismo instante.
