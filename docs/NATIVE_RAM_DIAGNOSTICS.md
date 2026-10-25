# Diagnóstico del juego desde memoria nativa

La investigación parte de una adquisición del target general y conserva sus
bytes para reproducir el bloque afectado fuera de Azahar. Un replay de CPU
es una prueba aislada: no acredita una pantalla GPU, entrada entregada en vivo,
partida controlable ni rendimiento en consola física.

## Carpeta de dumps (desde el 24 de octubre de 2026)

Atajos en partida (L solo no hace nada): **L+R+A** dump completo (pantallas,
RAM y VRAM), **L+R+B** dump parcial (pantallas y VRAM, sin RAM), **L+R+X**
borra los dumps. Full Dump viene activado por defecto; un valor explícito en
`settings.ini` lo sustituye. Mientras se escribe, la pantalla inferior muestra
el cuadro DUMPING con su barra de progreso.

Cada dump es una carpeta `sdmc:/3ds/sm64ds/dumps/NNN-dump-AAAAMMDD-HHMMSS/`
(`NNN` consecutivo según `dump-sequence.txt`):

| Fichero | Contenido |
| --- | --- |
| `info.txt` | Resumen legible: consola (Old/New, kernel, firmware, memoria de la aplicación), resolución, pantalla, Lock FPS, controles, escena y memoria libre. |
| `top-screen.bmp`, `bottom-screen.bmp` | Pantallas físicas (400×240 y 320×240) tal como las escanea el LCD. |
| `memory.vm` | RAM y VRAM completas (solo en el dump completo). |
| `lcd.bin`, `anchors.json`, `trace.bin`, `render.bin`, `upper.bin`, `lower.bin` | Los mismos contenidos que `.lcd`, `.anchors.json`, `.trace`, `.render`, `.upper` y `.lower` de la tabla siguiente. |

`build/claude-analysis/ds_layers.py <carpeta>` decodifica DISPCNT, BGxCNT y la
OAM de las dos pantallas DS de un dump.

## Dumps del juego integrado (desde el 23 de octubre de 2026)

Cada dump de Developer (L+R+A, Dump RAM o captura automática) congela el juego
en su frame hook después de vaciar el worker de presentación y escribe, con el
mismo nombre base `native-<tick>`:

| Fichero | Contenido |
| --- | --- |
| `.lcd` | Pantallas físicas: el framebuffer que cada controlador de pantalla escanea en ese instante (arriba ojo izquierdo/derecho y abajo), leído antes de pintar el progreso del dump. Incluye formato, stride, direcciones física/virtual y 1 KiB de registros GSP: identificación/relleno de memoria, PDC0, PDC1 y transferencia de display. |
| `.vm` (SM64VM1 v2) | Toda la memoria legible del proceso (código, datos, heap, pilas, heap lineal) más la VRAM completa (6 MiB, dos bancos) y las ventanas de memoria DSP, marcadas con `reason=4`. Los bloques de registros MMIO nunca se leen. |
| `.anchors.json` | Escena, epoch, anclas de símbolos, `physical_lcd` y contexto de consola: modelo, Lock FPS efectivo, kernel, firmware y región de memoria. |
| `.trace` | Anillo de tiempos por subsistema previo al dump. |
| `.render`, `.upper`, `.lower` | Lecturas de los targets de render (no son la pantalla física). |

`tools/inspect_integrated_vm.py <dump.vm> --elf <elf> --export <dir>` escribe
los PNG de las pantallas físicas en su orientación real, un JSON con registros
y estados, y `vram.bin`. Los dumps v1 siguen siendo legibles; no contienen VRAM
ni `.lcd`. La GPU, el DSP y el SO no se detienen de forma atómica: la VRAM y la
memoria DSP se leen después del vaciado del worker, pero el hilo de audio sigue
activo.

Validación del 23 de octubre: candidato `c2451eeae5c27fda`, adquisición
`build/integrated-native/acquisitions/attempt-1792808928023918000/`, perfil
Azahar Old. Cerró sin fallo registrado en 277,45 s: tres capturas programadas
y tres de inspección (Display/30, juego/60 y juego/OFF). Los seis conjuntos
contienen 120.692.736 bytes de payload legible, incluidos 6.291.456 bytes de
VRAM y 65.536 bytes de ventanas DSP mapeadas. Se verificaron hashes contra el
recibo de cierre, escena/epoch/tick de LCD frente a VM y cobertura de sus
direcciones. Se abrieron los PNG reales de juego y Display; el inferior conserva
la imagen anterior a la barra de progreso. Ambos ojos usan el mismo buffer con
estéreo desactivado, por lo que esta sesión no valida estéreo.

El lector y el analizador de campaña exigen para v2 ambos bancos completos de
VRAM y scanout superior izquierdo/inferior válido. Rechazan geometría, stride,
formato, tamaño, contexto o bytes inconsistentes antes de crear una imagen.
Los tests del lector (8 casos) y del lector de prefijos (4 casos) forman parte
del build central. Un v1 sigue siendo legible y queda identificado sin esa
evidencia adicional. Informe privado y PNG:
`build/claude-analysis/dumps-validation-2026-10-23/report.json`.

Esto captura la memoria accesible de la aplicación y el framebuffer enviado al
LCD. No es una fotografía, un volcado de toda la FCRAM física del sistema ni una
captura del kernel u otros procesos. Las regiones no legibles conservan su
descriptor y se identifican sin inventar payload. No se ha probado en consola.
La suma de payload virtual no es una medición del consumo de RAM física: el
mapa puede incluir alias y memoria compartida.

El detector automático distingue la cadencia solicitada: tres intervalos de
más de50ms o uno de más de100ms en Lock30; más de25/50ms en60 u Off. Cambiar el
lock reinicia el asentamiento, conservando cooldown y presupuesto de dos dumps.
La regresión ASan/UBSan sobre1.000 intervalos de33ms reproduce dos capturas
espurias con el código anterior y cero con la corrección. También comprueba
parones reales y cambio30→60. No se fabrica un parón dentro del juego para
probar este detector.

Regresión integrada posterior: candidato `6788c4dd9ed6898b` (ELF SHA-256
`6788c4dd9ed6898bd39948f2788ebe4a66e60fd7bf87ae10271ec561e77139fe`).
La adquisición `attempt-1792809804437522000` terminó en97,35s con tres bundles
v2 completos,1.791 filas estables y **cero capturas automáticas adicionales**
en la referencia de castillo Old/30. Los tres acreditan LCD superior/inferior,
VRAM completa, contexto Old y lock30, sin fallo registrado del emulador.
La ausencia de falsos dumps en esta referencia no prueba todas las escenas;
los parones reales siguen cubiertos por la regresión dirigida del detector.
Informe: `build/claude-analysis/dumps-validation-2026-10-23/final-6788c4dd.json`.

## Captura y consistencia

`NativeSnapshot` escribe el formato `SM64RAM1`: cabecera y descriptores LE32,
rangos físicos y vistas lógicas de las secciones originales. Las vistas dan
dirección nativa, dirección DS de origen, módulo, tamaño y offset del archivo;
no reconstruyen ni duplican memoria. El ELF exacto conserva código, datos de
solo lectura y símbolos para interpretar cada adquisición.

Se leen las secciones `.data`/BSS del ejecutable, el heap de aplicación libctru
y el heap lineal. El heap raíz original de 8 MiB, cachés de archivos, objetos,
buffers 2D, registros, VRAM, paletas y OAM quedan incluidos en esos rangos.
La residencia de la pila principal se consulta con una dirección local. Una
región ya cubierta se indexa aunque el mapa del sistema sea más amplio que la
pila; sólo una región adicional está limitada a 2 MiB. Ese dato no representa
un PC de fallo ni los límites exclusivos de la pila.

La versión actual del capturador añade la dirección TPIDRURW y los 512
bytes locales del hilo que llama, obtenidos mediante libctru y comprobados
contra los permisos del sistema. Este bloque puede ocupar un quinto rango;
los rangos y el formato LE32 siguen siendo explícitos. El índice distingue
`thread_tls_available` de la residencia de pila. Las adquisiciones anteriores
no contienen ese bloque ni se modifican para aparentarlo. La prueba host de
cinco rangos, sus canarios, el lector binario/JSON y la compilación ARM estricta
están en `build/native-integration-check/snapshot-tests-tls/report.json`.
La adquisición `1791624434443135000` ya conserva ese TLS real en sus tres
checkpoints. Usa cuatro rangos coalescidos y 234 vistas, incluidas las 226
secciones originales y una vista de 512 bytes del hilo llamante. Magic y
puntero reent residente pasan la verificación de integridad.

La simulación queda detenida durante la escritura y Citro3D se sincroniza
antes de ella. La copia es secuencial: los hilos de servicio y la contabilidad
de E/S pueden cambiar mientras se escribe. El heap lineal contiene los bytes
visibles para CPU; no se invalida todo el heap ni se atribuye coherencia de
lectura a buffers escritos por GPU. Los registros de texto omiten nombre y
cumpleaños; una copia completa de memoria puede contener el registro CFGU
original. Las adquisiciones permanecen locales.

Los rangos y permisos se comprueban antes de la E/S. Se usan escrituras de
hasta 64 KiB y un buffer fijo para JSON. Antes de reemplazar el binario se
invalida el índice anterior; una escritura incompleta no puede conservar un
manifiesto viejo marcado como completo. El lector compara los descriptores
con el JSON y rechaza truncados, solapes y vistas fuera de los rangos.

`NativeFrame` conserva RAM en tick 180, en el primer título listo y ante una
solicitud final de adquisición. Ante una función no disponible, la memoria
se escribe antes del registro `STOP:` y antes de liberar recursos. Un fallo
CPU externo no garantiza una captura en su instrucción exacta: se conserva
el último checkpoint completado y el primer fallo del emulador.

## Adquisición del 9 de octubre

El intento `build/native-runtime-azahar/attempt-1791613748756720000/` conserva
ELF SHA256 `a5c3b198603d8303c7322c0e398cda7f05103021bfe1aec411234303db1a75fe`
y 3DSX SHA256 `f8a75b8fc8eaf52e9d3fccd87c4569e80aa062430e677f2e963b84c68c3e25cd`.
Completó 180,155 segundos de adquisición con perfil Old, sin acceso de memoria
inválido, excepción CPU ni función no disponible observados. La detención
fue el límite temporal; el último registro periódico corresponde a tick 1050.

`ram-title-180.ram.bin` y `ram-final.ram.bin` contienen 98.116.880 bytes cada
uno, con índices completos de ticks 180 y 960. Sus tres rangos contienen
4.090.720 bytes de ELF RW, 60.465.152 del heap de aplicación y 33.554.432 del
heap lineal. Las 226 secciones originales y los cuatro buffers de vídeo
canónicos están cubiertos. La reserva de pila principal de 512 KiB está en
el comienzo del heap de aplicación: la dirección observada `0807e003` está
capturada. El índice de este primer binario marca `stack_available=false`
porque su consulta recibió una región mayor de 2 MiB; la corrección posterior
de metadatos acepta regiones grandes cuando sus bytes ya están cubiertos.
Las capturas originales no se reescribieron.

En ese intento del 9 de octubre, el control automático del ratón devolvió
`noWindowsAvailable`. `input.log` confirma que ningún toque llegó; B nuevo
llegó en tick 875 y se soltó en 877. La observación UI vio estrella y
«Touch to Start» abajo con ambos fondos negros. Ese intento no acredita la
transición al título listo. El toque del usuario que movió la estrella arriba
pertenece al intento previo `1791612346875167000` y se conserva como una
observación distinta.

## Adquisición del 10 de octubre: título listo con entrada normal

El intento `build/native-runtime-azahar/attempt-1791616229939712000/` conserva
ELF SHA256 `22a49e1552a15a6972cc0db302222ebd91ce58548cd0f0ec286beaff0708dde0`
y 3DSX SHA256 `1e09a3b63a79bd39db3743a88a22691579889b40856338fc4bea4a493ece0934`.
Usó perfil Old y duró 180,193 segundos desde el arranque de la aplicación,
sin fault de CPU/memoria ni STOP observados; terminó por el límite temporal.

Los tres dumps tienen 98.117.260 bytes cada uno: ELF RW 4.766.912, heap de
aplicación 59.789.312, heap lineal 33.554.432 y descriptores 6.604. Las 226
secciones originales y 233 vistas coinciden exactamente con la tabla ARM32
del ELF. Los rangos completos tienen hash verificado, y cada sección también
conserva su hash. Los índices nuevos acreditan la región cubierta que contiene
la pila. La menor reserva de heap frente al intento anterior compensa el mayor
RW del cache; no representa memoria del juego perdida en el dump.

| Dump | Tick | Estado observado |
|---|---:|---|
| `ram-title-180.ram.bin` | 180 | Introducción esperando el toque |
| `ram-title-ready.ram.bin` | 2810 | Main/sub cero, lock cero, opacidad 4096, título listo |
| `ram-final.ram.bin` | 2970 | Título listo, antes de la primera A |

`input.log` acredita E mediante HID táctil normal: (128,96) DS en 2337 y
liberación en 2343. La introducción avanza con sus callbacks y fades originales;
`title-state.log` observa la primera puerta lista en 2810. La ventana mostró
el logo en el fondo azul arriba y Mario con los botones abajo. Sólo llegó una
A nueva en 3036, después del dump final; se liberó en 3037. No llegó una segunda
A y no se afirma selección de archivo en esa adquisición.

`tools/analyze_native_acquisition.py` y
`build/native-integration-check/acquisition-622993.json` conservan la
comprobación completa de bytes, descriptores, aliases, secciones, raíces, pila,
vídeo y campos conocidos del título. La cobertura y la residencia no permiten
afirmar que todo objeto del heap siga vivo ni que se conozcan todos los fallos.

Los replays posteriores parten de `ram-title-ready` y del ELF archivado, sin
reiniciar la introducción. Dos A nuevas, separadas por liberación, pasan por
el foco original a Adventure y estado principal 5. Elegir la primera ranura
vacía produce el callback `af580`, resultado 3 y el `Behavior` original:
copiar plantilla de 44 bytes, `StartFile(1,0)` y petición de escena 3. Se
conservan los canarios, el alias de save de 14 bytes y los heaps protegidos.
Los archivos FAT se observan con metadata/payload reales del paquete; no se
simula éxito de FILE/TLS en Unicorn. La comparación distingue esa frontera de
libctru de un fallo del juego. Informes:
`build/native-title-video/ram-title-ready-622993-keyboard-filemenu.json` y
`ram-title-ready-622993-new-game.json`. La reproducción posterior ejecuta
fader, destrucción y petición de escena originales: el contexto y grafo del
menú terminan en cero y Stage alcanza su constructor. El fader `dFdDummy_c`
usa velocidad 4096 e ignora el argumento de duración; las dos llamadas
adicionales a `StartFile` durante la salida pertenecen al `Behavior` original,
sin reiniciar la petición de escena 3. La carga completa de Stage continúa
pendiente; estos replays no acreditan partida GPU.

La auditoría del título listo recupera la textura, los 2.274 triángulos y sus
UV desde la RAM. El atlas está orientado correctamente; el renderer conserva
las filas al subirlo pero usaba el origen V contrario para geometría 3D.
Esto explica la inversión observada en los botones y se corrige en el vertex
del renderer. La reconstrucción ideal de las filas de los botones no es un
resultado GPU ni una prueba de esquinas, wrap firmado o precisión float24.
La regresión del helper y del upload reales pasó con ASan/UBSan: 24 texturas,
6822 vértices, 993792 bytes completos de uploads y comparación completa del
atlas de botones sin diferencias. La fórmula anterior cambia 10633 píxeles.
El muestreador independiente también distingue truncación y `floor` en UV
negativas. No ejecuta PICA/float24, profundidad o mezcla completas. Informe:
`build/native-integration-check/title-ready-orientation/renderer-regression/renderer-orientation-test.json`.

El primer fallo reproducido al crear Stage es de ABI de tabla virtual. El
constructor manual guardaba `_ZTV5Stage` en vez de su primer slot callable,
ocho bytes después de offset-to-top y RTTI de GCC. El `Process` original lee
el slot de `BeforeInitResources` y salta a RTTI. Recompilar sólo los stores
manuales con el ajuste local `+2` permite ejecutar `BeforeInitResources` e
`InitResources` originales; no se desplazan aliases globales ni constructores
emitidos por GCC. Evidencia: `build/native-title-video/ram-title-stage-abi.json`.

Las copias generadas de nueve cuerpos gráficos de Stage también adaptan 69
sitios de registros y tres variables de puntero a registros, conservando
hashes originales y reconstrucción inversa. Las cargas raw de archivos
resuelven sus destinos de paleta/VRAM; `LoadCompressedFileAt` valida el span
de salida antes del descompresor original. La compilación general inicial de
este bloque pasó, todavía sin nueva adquisición integrada.

La siguiente frontera del replay es `__syscall_getreent`: su MRC lee
TPIDRURW, que la captura anterior no proporciona, y Unicorn devuelve cero.
La biblioteca exige magic `0x21545624` y devuelve el puntero real de TLS+8.
No se considera un fallo integrado de Stage ni se fabrica TLS para superarlo.

La revisión completa posterior clasifica 411 stores manuales de `_ZTV`: 351
con prefijo GCC real, 41 con address point ya explícito, cuatro aliases de
tablas ROM bare y 15 símbolos no retenidos. No procede desplazar símbolos
globales. `tools/native_vtable_rules.json` conserva hashes y offsets exactos
para las 351 reparaciones demostradas y la reparación transitoria dScene de
Stage ya revisada: 352 RHS en 287 fuentes. La transformación mueve ocho
bytes, conserva el tipo del RHS y verifica su reconstrucción inversa completa;
las tres declaraciones `char[]` no usan `+2` elementos. El gate de tipos y
ARM32 está en `build/native-title-video/vtable-recipe-test.json`. Los 59
controles restantes quedan sin cambios y Player conserva su tabla `data_`
original. La comprobación de factories en el ELF nuevo continúa por separado.

La regresión ARM del bloque de archivos ejecuta las seis cargas reales de
Stage en su orden original y compara 684.032 bytes completos de registros,
VRAM, paletas y OAM después de cada llamada. Las cuatro cargas LZ producen
64 KiB y ambas paletas 1.024 bytes. El baseline conservado descubre que
`LoadFileAt` copiaba bien la paleta, pero `func_02018568` volvía a leer su
dirección DS. El proveedor local traduce el span de cuatro bytes y ejecuta el
comparador original. Pasan ambas cadenas raw, 20 casos de lectura, nueve
fronteras comprimidas y 20 cabeceras/ABI. Informe y límites:
`build/native-file-video/file-video-arm-test.json` y `FINDINGS.md`. La apertura
SD/FAT/TLS, la GPU y el FSM completo de Stage no se ejecutan en ese fixture.

El candidato general de este bloque está compilado y congelado en
`build/native-integration-check/candidate-vptr-file-camera-tls/`, con ELF
`396ac3ff7f20af01d411357d97da9cdc4dccdee70f6fc53d1d8305a5804361c8`
y 3DSX
`554844c42674f53b0ccaed7a8e8a393569bb89780b70344e03d3e1b7e02cc65b`.
Incluye los proveedores de archivo, las operaciones FP y el cuerpo original
de Camera, además del TLS real en futuras capturas. No se ha lanzado; la RAM
anterior conserva su ELF exacto y no se instala sobre el layout nuevo.

## Frontera actual de Stage

El ELF de la nueva adquisición tiene SHA256
`cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29`.
`ram-stage-requested` guarda 98.093.592 bytes en tick 3859, antes de
SpawnIfNecessary y con el tick abierto. La fase original es 3, la escena
pendiente es 3, su parámetro es cero y el flag already-spawned es cero. El
contexto de título y su callback están destruidos. El puntero de escena
residual residente no se interpreta como objeto vivo sin revisar su lifecycle.

Después del registro de escritura completa no terminó otro frame antes del
límite de 360 segundos. No hay checkpoint `ram-stage-presented`, PC live del
bloqueo ni excepción/STOP explícitos observados. Estos datos acotan la frontera
de investigación, pero no identifican todavía su causa. Los replays nuevos
usan únicamente la RAM y el ELF exactos de esta adquisición, con su TLS real;
no instalan la RAM anterior sobre el layout nuevo ni fabrican respuestas de
servicios para afirmar que Stage terminó.

Los otros dos checkpoints son tick 180 y título listo en 3270, con el mismo
tamaño. `build/native-integration-check/acquisition-443444.json` verifica todos
los bytes por hash de los 30 archivos archivados y acredita las dos A nuevas
en 3312/3448 y los toques normales en 2792/3748. La política sigue siendo una
adquisición amplia, diagnóstico completo offline y reparación del bloque
causal antes de otra ejecución integrada.

## Ciclo del registro de archivos demostrado

La cabeza `data_020a8048` capturada apunta a `0x08a81940`, dentro del payload
libre del root heap; el record ROM estático `data_020a8074` tiene sus 80 bytes
iniciales a cero. El replay de ese mismo ELF/RAM entra en Stage Init dentro de
Spawn. Su primera variante se detiene honestamente ante `svcSendSyncRequest`
de FSFILE_Read. La continuación declara sólo una lectura offline ligada al
FILE/descriptor/handle ya capturados y al paquete EU verificado. Conserva
stdio, vector, allocator, CRC, parser NARC y registro de producción.

El primer NARC, `c2d`, reutiliza exactamente la dirección de aquella cabeza.
Al registrar su nombre, las instrucciones originales escriben esa dirección
en NEXT y PREV del propio nodo. El montaje `cee` busca una clave distinta y
visita de nuevo el mismo nodo. La traza detiene la repetición tras 1.403.047
instrucciones; otra variante alcanza cuatro millones en el mismo ciclo.
No se limpió o reparó la lista en la RAM clonada ni se forzó retorno de Spawn.

El contrato original coloca una entrada ROM permanente antes de los NARC.
El proveedor nativo omitía esa entrada. Unregister conserva la lógica original
de vecinos y no debe modificarse para esconder la ausencia del primer nodo.
La corrección de producción registra la ROM antes de publicar el catálogo y
valida las llamadas repetidas. El nuevo ELF compila y pasa revisión estática;
su gate ARM de lifecycle pasó 17 casos con estado fresco y archivos reales
(276.028 instrucciones). La adquisición posterior `1791627213451636000`
conserva cuatro dumps completos de 98.089.496 bytes, incluido STOP. En esa RAM
el registro contiene ROM → c2d → cee → ar0 → ar1 y no forma un ciclo.
El STOP identifica `__sinit_d_a_star_gate.cpp`; no se presentó un frame Stage.
Su auditoría de 32 archivos está en `build/native-integration-check/acquisition-721345.json`.
Los tres inicializadores de TUs recuperadas sin nombre GCC compatible se
restauraron como conjunto y pasaron 18 casos/24 callbacks ARM sobre el ELF final;
la adquisición posterior `1791628210782988000` supera esas entradas y alcanza
STOP en `ShadowModel::InitCuboid`. Conserva cuatro dumps de 98.089.528 bytes,
verificados contra el ELF exacto. Camera y Player tienen sus vptrs correctos
en esa RAM; el registro de archivos permanece coherente y ov100 está activado.
Eso acredita avance de inicialización, sin un frame Stage presentado.
`data_0209f314` apunta a los slots de transformación de textura en Stage+0x8bc;
el modelo empieza en Stage+0x86c, según Stage.h y Stage::InitResources. Se
corrige la etiqueta anterior de "modelo", sin cambiar los valores capturados.
`data_02099e70` se
observa como escalar y no se interpreta como puntero de escena. Informes:
`build/native-integration-check/acquisition-821078.json` y
`acquisition-821078-registry-corrected.json`; el informe original se conserva.
El cuerpo y los aliases reales de la sombra
cúbica se incorporaron y pasan diez casos ARM con buffers completos, lifecycle
y fallos de memoria. La adquisición integrada `1791629860058059000` completó
245,629 segundos, cuatro dumps completos y auditoría de 32 archivos. STOP
identifica la fábrica 1-Up en tick 2663. La RAM registra 60 modelos comunes;
Cube/Cylinder tienen buffers de 148 bytes con identidad y SHA-256
`a01413f5b40070fe60a99fb2a2a80ed12a5e56bc52dcbadfb5f5d0b114cd04f4`,
y materiales `1f8080`. Informes: `acquisition-986005.json` y
`acquisition-986005-shadows.json`, en `build/native-integration-check/`.
La unidad de 1-Up/Mega Mushroom ya compila completa y su ELF final pasó ocho
casos ARM de construcción/destrucción/OOM contra ASMP0; no se trasladó esta
RAM al candidato nuevo. La adquisición `1791631629219652000` completó
250,369 segundos y STOP en la fábrica de MIPS, tick 3224, sin fallo CPU/memoria.
Sus cuatro dumps de 98.085.432 bytes pasan la auditoría de 32 archivos.
`acquisition-162921-actors.json` recorre la lista real: 61 actores, sin ciclos,
enlaces prev/next/owner coherentes y tres hongos vivos con tipo 3 y componentes
cargados. El puntero de archivo en ModelBase a cero indica propiedad prestada;
no se interpreta como prueba de un modelo descargado. Aún no hay Stage presentado.
La revisión de tablas de objetos en `acquisition-162921-level-factories.json`
cubre 72 entradas y 18 fábricas de cinco categorías. MIPS era la única ausente;
el candidato nuevo incluye las 18. No cubre otras categorías ni spawns procedurales.
El bloque completo de MIPS compila 32 funciones y pasa diez casos ARM contra
ASMP0, incluyendo 1.140 bytes construidos, animación, matrices/sombras, globales,
listas, OOM y cleanup de handles vacíos. No ejecuta I/O ni frames del juego.
El supervisor exige ahora la igualdad entre el 3DSX y un empaquetado nuevo del
ELF exacto antes de arrancar. Pasó también para las tres capturas anteriores.
Esos gates no demuestran un Stage live ni hardware físico.

La adquisición posterior `1791633566550586000` conserva tres dumps completos
de 98.077.240 bytes y una escritura stop interrumpida (2.668 bytes e índice
vacío). La auditoría verifica los 32 archivos conservados, pero no intenta
leer esa escritura parcial como RAM válida. El checkpoint pre-Spawn de tick
1482 corresponde al ELF `a9328cfa…`. Su continuación aislada con los mismos
bytes de paquete reproduce Read8 de `0xfd6ed0d9`, PC efectivo `0x1cdbf4`.
El PC live reportado `0x1cdbc8` pertenece al caller y no es la instrucción de
lectura. La captura previa todavía tiene cero actores; no prueba MIPS live.

El recurso SPA capturado difiere de ARM9 en sólo dos flags de definiciones,
`+0x6920/0x6974`. Sus valores parecen direcciones, pero el formato los define
como Flags1. Las relocaciones falsas cambiaron el stride de la definición 306
de `0x54` a `0x64`; el cursor termina en `0x6db8` en vez de `0x6e60` y luego
lee `0xfcfcfad9` como tamaño de textura. La suma u32 deriva exactamente el
puntero `0xfd6ed0d5` y la lectura `+4` observada. No se normalizan esos flags
como punteros. Informes: `acquisition-356655.json`, `acquisition-356655-actors.json`,
`acquisition-356655-spawn-replay-libc.json` y `particle-fault-review.json`.
El replay declara su stack aislado y backend de archivos/copia libc; la libc
del ELF se verificó primero con 44 casos ARM. No es una reanudación live de PC/GPR.

El candidato corregido `candidate-particle-flags` pasa el gate ARM y conserva
los 49.684 bytes de la SPA publicados por el cargador. La arena y los uploads
originales/nativos coinciden para ambos modos, con canarios y ABI verificados.
La adquisición integrada `1791635512435132000` completó cuatro dumps. El lector
comprobó los 39 archivos conservados. `acquisition-551243-particles.json` verifica
la SPA entera en las cuatro capturas y los 2.533 campos de puntero revisados del
manager inicializado en tick 3130: 321 definiciones, 280 behaviors y 46 texturas.
Su arena usa 35.700 bytes y deja 140 libres. `acquisition-551243-actors.json`
verifica los enlaces de una lista de 90 actores; no convierte el recuento en
prueba de inicialización completa o control. El fallo siguiente fue el acceso
GX de `func_02044534`; el checkpoint `stage-presented` no prueba Stage visible.

El gate `candidate-model-billboard/model-billboard-arm-gate.json` compara 13
casos de esa rutina con ARM9 original antes de otra ejecución integrada. El
candidato conserva cada comando, los tres parámetros SCALE y el FIFO completo.
En su adquisición `1791636898304174000` se ha observado la carta original de
Peach tras escoger File B NEW, seguida del castillo, Lakitu y el mapa inferior.
La adquisición terminó por límite tras 360,030 segundos, sin fallos de acceso
a memoria de CPU registrados, con cinco dumps completos de 98.077.240 bytes.
`acquisition-898304.json` verifica los 41 archivos conservados;
`acquisition-898304-actors.json` verifica las listas antes y después de 31
destrucciones pendientes; `acquisition-898304-particles.json` comprueba la SPA
entera y los 2.533 campos de puntero revisados en los dos managers inicializados
capturados. Control y escenario jugable siguen sin acreditarse. No se importa
RAM antigua en el ELF nuevo ni se atribuye
al gate prueba de frame, IRQ timing o hardware.

Evidencia inmutable: `build/native-integration-check/stage-requested-443444/READOUT.md`,
`CAUSAL-PROOF.json` y `captured-pack-registry-proof-replay.json`.
El defecto del registro está demostrado; el PC live sigue sin captura y no se
atribuye al replay una prueba de la causa exclusiva de la espera integrada.

## Causas comprobadas en la RAM histórica de la introducción

| Observación | Estado real y comprobación |
|---|---|
| Fondo superior negro | El efecto original es tipo 4, motor B, estado 3 de mantenimiento, con ambos extremos `-16`, máscara `2f`, sin petición o callback pendientes. `BLDCNT_B=ef` y `BLDY_B=16` mantienen el negro. Noventa despachos aislados de `bf690` por captura conservan exactamente ese estado. |
| Fondo inferior negro detrás de la estrella | Los widgets originales `ctx+108/10c` tienen cuatro colores de esquina cero y alfa `1f000`. El stream GX real emite rectángulos negros opacos antes de la estrella. La capa 2D blanca queda detrás de esa geometría. |
| Introducción esperando | En ambas capturas el callback original `b0da0` sigue activo; BCA está pausada en frame 0 de 157, velocidad fija 4096 y contador del toque cero. Los contadores de subestado avanzan 56 → 836: el bucle sigue ejecutándose y espera una entrada válida. |
| B no salta la espera | El registro de partida original tiene las tres ranuras y el word de estado a cero. Su predicado devuelve 1; la rama genérica de salto con A/B exige otra condición. El comportamiento coincide con la ROM. |
| Logo todavía negro | Tiles y ambos mapas coinciden con la ROM, las capas están activas y los nodos de cola están limpios. El replay del compositor con OAM real coincide byte por byte con los tres scanouts de tick 180. El fundido mantenido explica el negro del motor B. |

Un replay de la copia de RAM ejecuta seis casos por checkpoint: sin toque,
A nuevo, B nuevo, dos coordenadas exteriores y un toque dentro de la estrella.
Sólo el último retira el callback y avanza BCA. Desde tick 180, el toque
controlado en el contador 57 retira el callback en 60 y solicita subestado 10
en 364; desde tick 960 ocurre en 840 y 1144. La animación alcanza frame 151 de
157. Los canarios verifican todos los bytes de los tres rangos fuera de los
campos documentados de entrada, estado y animación. Estos son eventos en una
copia controlada; no se presentan como entrada o pantallas de Azahar.

La observación anterior de estrella arriba y fondos negros también cabe en la
introducción original: con toque en contador 26, el callback termina en 40 y
en contador 206 BCA todavía está en frame 82 de 157. Las pruebas de 60 segundos
terminaban antes de completar esa secuencia con la cadencia emulada observada.

Herramientas persistentes: `tools/decode_native_title_ram.py`,
`tools/test_native_title_ram_arm.py`, `tools/test_native_title_fade_arm.py` y
`tools/audit_native_title_video.py`. Los informes de la RAM real están en
`build/native-title-video/`. El resto del flujo de título, selección de archivo,
partida, audio, cámara y rendimiento físico se evalúa por separado; esta
evidencia no atribuye corrección a rutas que aún no han ejecutado.


## Continuación C13/C14: vídeo de cutscene y caché de texturas

C13 conserva el ELF exacto y cuatro capturas completas de 98.077.304 bytes.
Los lectores revisan íntegramente los dumps, la lista de 90 actores y la SPA;
el lector de caché observa 27 bindings sin modificar RAM. La reproducción
controlada de `func_ov002_020f1fcc` identifica la lectura directa de DISPCNT
sub DS `0x04001000` en el último paso del fade. No se usa el snapshot temprano
como si fuera RAM del instante del fallo.

C14 lleva doce rutinas al backend nativo de vídeo; once pasan 63 casos ROM/ARM
y la rutina de recursos de créditos pasa seis idiomas y todos los bytes de
vídeo y buffers. El manifiesto cambia sólo la selección de esos doce objetos;
se conservan 226 secciones, 949.448 bytes de datos, 24.031 fixups y las dos
exclusiones SPA verificadas. Las tablas nativas, seis objetos de cámara y
billboard no cambian; once proveedores conservan sus instrucciones revisadas.

Candidato: `build/native-integration-check/candidate-cutscene-video/`.
La adquisición `1791640481103529000` está en curso; el estado final se registra
en su `result.json` y los lectores posteriores. No hay prueba de hardware,
control completo, audio, guardado ni 60 FPS sostenidos en Old 3DS.
