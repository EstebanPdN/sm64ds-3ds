# Integración general ARM11 — desarrollo

El objetivo sigue pendiente. `sm64ds-native.3dsx` integra el arranque, las
escenas y el bucle de actores originales. El título original ya termina su
transición con entrada normal en Azahar. La selección de archivo y la petición
de nueva partida también llegaron en la última adquisición integrada. Todavía no acredita partida
controlable ni rendimiento físico. El CIA del visor en `dist/` conserva
su identidad anterior. Esta integración no es una release.

Prioridad actual del usuario: una sola vista normal. La integración general
fuerza el deslizador a cero y las pruebas usan salida mono; la estereoscopía
queda aplazada hasta que el juego normal funcione.

El objetivo final, reafirmado por el usuario, es el juego completo funcionando
correctamente a 60 FPS sostenidos en una Old 3DS física, con la velocidad
normal de simulación. La cámara con C-stick debe ofrecer giro e inclinación
continuos, libres y fluidos en los modelos que lo incorporan. Publicar el
stick para la cámara original no satisface por sí solo ese requisito. La
potencia relativa del hardware no sustituye la medición del port terminado.

## Construcción y ejecución

Se usa la revisión fijada `6230b2996636e3fd0e03cb178161c94cb4b73cc9` de
`tangosdev/sm64ds-decomp`. `SM64DS_NATIVE_RUNTIME=ON` activa un target separado
del visor. Requiere el corpus ARM11 y los adaptadores de colisiones/entrada ya
compilados en `build/native-core`, `build/native-collision` y
`build/native-adapters`.

```sh
cmake --build build/3ds --target sm64ds-native_3dsx --parallel 4
```

`tools/build_native_runtime.py` selecciona los objetos originales y sustituye
sus proveedores de hardware mediante símbolos explícitos. El manifiesto
enumera funciones todavía no disponibles y referencias sin destino resuelto.
Una función no disponible detiene el proceso con su nombre. No se permiten
definiciones duplicadas para resolver la integración por orden de enlace.

`tools/native_vtable.py` aplica una receta revisada a las copias generadas del
corpus y de escenas. Las 352 expresiones manuales de 287 fuentes cambian
únicamente el RHS al primer slot GCC, ocho bytes después de su cabecera.
Los hashes y offsets originales se comprueban antes de cada transformación y
la reconstrucción inversa conserva todo el resto del archivo. Los 59 stores
de control quedan sin cambios: tablas bare de DS, casos ya ajustados y
símbolos no retenidos, salvo el store transitorio de Stage ya revisado. No
se desplazan símbolos de tabla globales ni las tablas `data_` de Player.

El candidato congelado `candidate-vptr-file-camera-tls` tiene ELF
`396ac3ff7f20af01d411357d97da9cdc4dccdee70f6fc53d1d8305a5804361c8`
y 3DSX
`554844c42674f53b0ccaed7a8e8a393569bb89780b70344e03d3e1b7e02cc65b`.
Enlaza las cargas raw/comprimidas y su peek de cabecera adaptado, el cuerpo
original de Camera, los dos helpers software FP y el soporte de TLS de futuras
capturas. La compilación y el enlace pasan; este candidato no se ha ejecutado.

Las tablas de datos incluyen 226 secciones, 949.448 bytes, 24.033 reubicaciones
y 305 inicializadores originales. Los destinos de overlays ambiguos se
seleccionan al activar el módulo. Los inicializadores C++ automáticos emitidos
por GCC para fuentes recuperadas se retiran de los objetos aislados: los
inicializadores originales se llaman después de restaurar los datos de la ROM.
Veinticinco palabras en tres archivos gráficos Nitro y un stream LZ10 se
excluyen con dirección, módulo y valor exactos: son bytes de gráficos
confundidos con punteros por el metadato recuperado. No se filtran direcciones
por heurística. La publicación de los globals compartidos con el visor ocurre
después del catálogo, mediante el inicializador de perfil ROM ya verificado.

El cargador actual acepta exclusivamente la ROM EU ASMP0 comprobada
(CRC32 `29715dec`). No extrapola direcciones desde la ROM USA. Los recursos se
leen desde `sdmc:/3ds/sm64ds/native/assets.pak`; no se incorporan al ejecutable.
Los operadores privados del juego mantienen separado su heap de las
asignaciones C++ de la plataforma.

`tools/native_3dsx_elf.py` retira únicamente los registros de reubicación ELF
que describen identificadores absolutos de overlay, comprobando el símbolo,
tipo y valor. Conserva los bytes ejecutables; evita que `3dsxtool` interprete
el número de overlay como una dirección para reubicar.

`tools/run_native_game.py --azahar RUTA --seconds 30` conserva un binario por
intento, su SHA256 y sus registros. Rechaza repetir el último binario idéntico.
Se detiene al primer fallo de memoria/CPU del emulador, un STOP de la
aplicación o el límite temporal. El arranque del emulador tiene un límite
separado (`--startup-seconds`, por defecto 20); los segundos de ejecución se
cuentan desde el primer registro de la aplicación. El cierre del proceso
se contabiliza por separado. Una adquisición puede durar hasta 360 segundos
para cubrir las fases originales; la escritura RAM en curso tiene una gracia
separada y acotada (`--snapshot-grace-seconds`, por defecto 30). Se solicita un
checkpoint final 20 segundos antes del límite. START+SELECT permite salir
manualmente. Los índices completos se conservan en el resultado, y la salida
de terminal sólo muestra un resumen.

`--keyboard-touch` configura sólo el perfil aislado de Azahar del intento:
la tecla E produce un toque HID normal en (160,120) del panel 320×240,
que el adaptador convierte a (128,96) DS. No altera globals del juego. El
perfil exacto y su hash se conservan; la entrega se acredita en `input.log`,
no por la presencia de la configuración. La adquisición de 180 segundos no
se repite mientras la RAM permita resolver el siguiente bloque fuera de ella.

## Servicios integrados

- `NativeBoot` conserva la secuencia original y divide el bucle en su propia
  espera de VBlank. La escena se elige mediante la tabla original de actores.
- `NativePlatform` ofrece un contexto, reloj, IRQ y colas nativas. No emula un
  ARM7 ni inventa respuestas de firmware. Las esperas fuera de esta frontera
  requieren adaptadores adicionales.
- `NativeFrame` entrega entrada y geometría a Citro3D. La simulación mantiene
  el divisor temporal original; presentar dos ojos no ejecuta dos ticks.
- `NativeVideo` reserva registros, VRAM, paletas y OAM en memoria nativa. Los
  servicios de copia y selección de bancos convierten accesos DS a estos
  buffers y al stream GX. El compositor 2D reutiliza el del port de PC.
- `NativeCapture` recibe una imagen GPU real de engine A a 256×192, antes de
  la selección de pantalla y el brillo maestro. La composición usa un target
  dedicado, sin avanzar otra pose. El backend escribe BGR555 en el banco LCDC
  elegido por el juego y conserva la semántica de crop, wrap y finalización.
  Source B, mezcla A+B, captura 3D sola y blank forzado no están integrados en
  esta ruta: los casos correspondientes fallan explícitamente.
- `NativeCard` abre los bytes originales del paquete, mantiene posiciones y
  montajes NARC reales y corrige contratos de argumentos dependientes de MWCC.
- `NativeSave` implementa el medio EEPROM de 8192 bytes en
  `sdmc:/3ds/sm64ds/native/save.bin`; el juego conserva sus checksums, ranuras y
  copias. La ausencia del archivo representa un medio borrado. Su persistencia
  está pendiente de verificación de ejecución.
- `NativePersonal` lee una vez idioma, nombre y cumpleaños mediante CFGU y
  exporta el registro original de 80 bytes. La ROM EU usa sus seis idiomas;
  otros idiomas válidos de 3DS seleccionan los recursos ingleses. No se guarda
  nombre ni cumpleaños en los registros de texto. Las copias locales completas
  de RAM pueden contener ese registro original de CFGU.
- La matriz de cámara `G3i::LookAt_` conserva sus operaciones y orden originales
  y publica los 13 comandos/parámetros en el stream GX nativo. Su contrato de
  traducción y sus variantes de alias se compararon con las instrucciones ROM.
- El audio está explícitamente desactivado. El C-stick se captura y su control
  de cámara libre está integrado con gate CPU; falta verificarlo en una partida
  del target y en los modelos con C-stick. La geometría superior tiene una
  proyección 400×240; las capas DS 2D conservan proporción, con márgenes.
- El capturador conserva RAM completa antes del SpawnIfNecessary
  de la primera petición original de escena 3, con el tick abierto, y otro
  dump tras su próxima presentación terminada. Guarda stream, vídeo y scanout.
  El evento significa petición,
  no carga de Stage terminada. Los logs y metadatos incluyen ready, generación,
  actividad manual, bloqueos, colisiones, ángulos, radios y tiempo lógico de la
  cámara; esos valores se leen en el mismo hilo que actualiza el juego.

## Evidencia y límites

La adquisición de referencia del registro es `1791624434443135000`, con ELF
`cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29`
y 3DSX
`fddec3723c4646fcea5a7d3904c8e3380d078019e2c7d72572553161f1319346`.
Se ejecutó una vez, con perfil Old y límite de juego de 360 segundos. Llegaron
el toque de la estrella en tick 2792, dos A nuevas en 3312 y 3448 y el toque
de File B NEW en 3748. El título quedó listo en 3270 y su contexto se había
destruido al solicitar escena 3 en 3859. Se observaron el título, los botones
derechos y el menú de archivos; el supervisor no convierte esa observación
humana en un gate automático de imagen completa.

Los tres dumps de ticks 180, 3270 y 3859 tienen 98.093.592 bytes cada uno.
Incluyen las 226 secciones originales y el TLS real del hilo llamante de
512 bytes, con magic y puntero reent residente comprobados. El checkpoint
3859 está antes de Spawn, con el tick abierto: no prueba Stage inicializado.
Después de terminar su escritura no hubo otro frame ni checkpoint posterior
antes del límite. No se observó STOP ni excepción CPU explícita; el proceso
se cerró por el supervisor. La causa del bloqueo se investiga offline desde
ese ELF y esa RAM; no se atribuye un fallo integrado a un replay limitado por
servicios de biblioteca. Verificación íntegra de los 30 archivos:
`build/native-integration-check/acquisition-443444.json`.

El replay posterior de esa RAM ejecuta Spawn, constructor y Stage Init
síncronos. Con un único backend offline de FSFILE_Read ligado al descriptor
ya capturado, lee y verifica los NARC `c2d` y `cee`. El primer montaje reutiliza
la cabeza del registro capturado, que estaba en memoria libre; el registro
original escribe NEXT/PREV hacia sí mismo y la búsqueda del segundo archivo
entra en un ciclo puro. No se forzaron objetos, resultado de Spawn ni estados
de escena. La primera variante con I/O intacto se detuvo en el SVC real de
lectura; la variante con bytes verificados declara expresamente ese backend.
El proof no contiene el PC del bloqueo live ni demuestra una causa exclusiva.

El arranque original registra primero la identidad `rom` estática, incluso
antes de validar FAT/FNT. El proveedor nativo omitía ese tramo. La reparación
añade `sm64ds_native_register_rom_archive` y lo llama antes de publicar el
catálogo. Usa `func_0205cd34/func_0205cc80` originales, comprueba errores y
conserva la cadena en llamadas repetidas. No modifica unregister ni publica
flags SDK de archivo cargado o callbacks DS: NativeCard sigue siendo dueño
del transporte desde el paquete real. El ELF corregido
`1e1eef06dff21c7a4ded69c93366f5c3d2718f8190d129abe06ffb94dec78ba0`
compila y tiene revisión estática aprobada. Su gate independiente ARM pasó 17
casos con estado inicial fresco, sin RAM vieja sobre ELF nuevo; conserva
parser, registro, búsqueda y retirada originales, con transporte y allocator
acotados y declarados. La adquisición posterior `1791627213451636000`
conserva cuatro dumps completos y alcanza STOP en `__sinit_d_a_star_gate.cpp`.
La lista en RAM es ROM → c2d → cee → ar0 → ar1, coherente y sin el ciclo;
todavía no termina ni presenta un frame Stage. La auditoría de integridad de
32 archivos está en `build/native-integration-check/acquisition-721345.json`.
Evidencia: `build/native-integration-check/stage-requested-443444/CAUSAL-PROOF.json`
y `build/native-integration-check/candidate-rom-archive-anchor/build.json`.

Las tres entradas MWCC de inicialización sin proveedor son Battan (ov023),
Kb1 billboard (ov044) y Star gate (ov100). Sus TUs recuperadas compilan, pero
GCC nombra localmente sus inicializadores `_GLOBAL__sub_I_*`; el aislamiento
retira `init_array` para impedir ejecución antes de restaurar los datos ROM.
`native_file_structors.cpp` restaura esas entradas de la tabla de módulos,
con los constructores y callbacks de limpieza exactos del manifiesto upstream,
nodos nativos de tres palabras y los cinco estados con PMFs ARM reales.
Los vectores POD conservan su almacenamiento y el initializer no los modifica.
El candidato posterior compila y su ELF final pasó 18 casos y 24 callbacks
contra los cuerpos ASMP0, sin recompilar producción ni importar RAM capturada.
El gate sólo ejecuta esos inicializadores y cleanup de handles recién creados;
no es prueba del manager de activación, recursos cargados, Stage ni hardware.
Evidencia: `build/native-integration-check/candidate-overlay-static-init/ARM-gate.json`.
La adquisición `1791628210782988000` superó esas entradas y alcanzó STOP
en `ShadowModel::InitCuboid` durante el spawn. Conserva cuatro dumps completos
y la auditoría de 32 archivos; el registro de archivos sigue coherente y
Camera/Player existen con las tablas virtuales esperadas. Su inicialización
y un frame Stage completo todavía no están demostrados.

La receta nativa añade el `InitCuboid` recuperado y resuelve las dos plantillas
de sombra de arm9 contra los símbolos exactos de ov001, `020ad524/020ad560`.
El ELF final de `candidate-shadow-cuboid` pasó diez casos contra instrucciones
ASMP0, incluidos parsing, matrices, materiales, caché, lista, cleanup y OOM con
reintento. Se compara todo el buffer de componentes; no se importa RAM vieja.
GX Begin/End de paletas son observadores declarados de dispositivo porque las
plantillas reales no tienen texturas ni paletas. El gate no valida GPU.
La adquisición integrada `1791629860058059000` completó 245,629 segundos y
STOP en la fábrica 1-Up, tick 2663. Sus cuatro dumps/32 archivos están
verificados. El registro de modelos comunes creció a 60; ambas sombras
conservan sus componentes, materiales, matrices de identidad y buffers de
148 bytes, de acuerdo con el gate. Stage presentado sigue pendiente.

El bloque completo `actors/da1up_c.cpp` compila sus 38 funciones después de
adaptar 91 declaraciones locales con alias de ensamblador reversibles; se
mantienen tipos, cuerpos y símbolos originales. Su `new` devuelve NULL por
contrato del juego, pero GCC suponía éxito por la declaración recuperada sin
`noexcept`. La prueba ROM/ELF confirmó escritura en cero antes del arreglo;
`-fcheck-new` sólo para esa TU conserva las dos comprobaciones originales.
El ELF final de `candidate-mushroom-nullguard` pasó ocho casos: construcción
completa de 920 bytes, 31 slots virtuales, listas, D1/D0 y OOM. Cuatro palabras
de vptr después del final de vida tienen valores distintos, comprobados por
separado; los otros 904 bytes destruidos coinciden. No se ejecuta InitResources
ni se simula éxito de carga. El intento integrado `1791631629219652000` completó
250,369 segundos sin fallo CPU/memoria y STOP en `daMip_c_classInit`, tick 3224.
Los cuatro dumps completos pasan la auditoría de 32 archivos. La lista de
61 actores conserva enlaces coherentes; los tres hongos tienen estado vivo,
tipo 3 y componentes cargados, evidencia de InitResources real. Stage presentado
y control siguen pendientes. `verify_native_3dsx.py` vuelve a empaquetar
independientemente el ELF y exige igualdad del 3DSX antes de cada arranque.

MIPS es una unidad de ov085 con 32 funciones y 63 declaraciones locales que
requieren vistas de datos de tipos distintos. El generador usa alias al símbolo
original y referencias locales tipadas; el puente se invierte byte por byte y
no crea almacenamiento de juego. Su ELF final pasa diez casos contra instrucciones
ASMP0: construcción de 1.140 bytes, NULL, animación con BCA original prestada,
matrices/sombras, listas, globales y cleanup de siete handles vacíos. Cinco vptrs
terminales se verifican separadamente; el resto de los 1.120 bytes destruidos
coincide. No se importa RAM vieja ni se ejecuta I/O o un frame en ese gate.
`acquisition-162921-level-factories.json` examina 72 entradas seleccionadas de
cinco categorías del nivel: de 18 fábricas distintas, MIPS era la única ausente.
Las 18 están presentes en el candidato; eso no demuestra su inicialización.
`candidate-mip-unit/mip-arm-gate.json` conserva el gate. Su prueba integrada
`1791633566550586000` alcanza una lectura inválida de `0xfd6ed0d9` en la carga
de partículas de Stage. Se conservan tres dumps completos y un cuarto archivo
interrumpido, excluido explícitamente. La continuación aislada del ELF y RAM
exactos llega al mismo acceso en `func_0204a17c+0x88`, dentro del parser SPA.
El formato demuestra que `SPA+0x6920/0x6974` contiene Flags1 de las definiciones
306/307. La metadata reemplazaba ambos `0x02083620` como si fueran punteros,
cambiando el recorrido y haciendo leer un tamaño de textura `0xfcfcfad9`.
`particle-fault-review.json` conserva el puente hasta la dirección inválida.

`build_native_runtime.py` conserva ahora esos dos flags originales mediante
exclusiones exactas, sin alterar upstream. `test_native_embedded_spa_arm.py`
verifica el recurso completo publicado por el cargador y ejecuta el constructor,
allocator y parser originales/nativos para ambos modos. Sus 321 definiciones,
280 behaviors y 46 texturas producen arenas idénticas tras normalizar únicamente
campos tipados comprobados. Los uploads observados también coinciden. La arena
válida necesita `0x8b74/0xa7b4`, por debajo de `0x8c00/0xa800`. El gate pasa en
`candidate-particle-flags/embedded-spa-arm-gate.json`, ELF `daf2f6fe…`; no ejecuta
frames ni simulación de partículas. Su adquisición integrada `1791635512435132000`
completó cuatro dumps; la SPA live conserva todos sus bytes y 321/280/46 registros
con punteros comprobados. La arena tiene 140 bytes libres y la lista contiene 90
actores con enlaces coherentes. El fallo siguiente está en la rama billboard
`func_02044534`, que aún escribía directamente en GX de DS. Sus stores sin
volatile perdían dos parámetros SCALE en el ELF, además de alcanzar después
la cola GXSTAT/IRQ de la transferencia SDK.

`candidate-model-billboard` conserva los siete comandos directos, las tres
escalas y su orden mediante `sm64ds_draw_port`. En el único callsite
`func_0205a358(3,src,bytes,0,0)` seguido inmediatamente por WaitDMA, emplea el
FIFO síncrono nativo y retiene la espera nativa. No se introduce un shim global
del protocolo SDK ni una región MMIO ficticia. El gate del ELF `cbdb84c3…`
compara 13 casos con ARM9 original; sólo modela el dispositivo aritmético DS y
ese transporte SDK sin callback en el lado original. El lado nativo ejecuta su
buffer y espera reales con MMIO DS sin mapear. No acredita IRQ timing, raster,
frame de Stage, control ni FPS. Su adquisición integrada `1791636898304174000`
terminó por límite tras 360,030 segundos sin fallos de acceso a memoria de CPU
registrados. Se conservaron cinco dumps completos de 98.077.240 bytes. Las
auditorías comprobaron 41 archivos, la SPA completa, los 2.533 campos de puntero
de cada manager inicializado y las listas de actores. La carta de Peach, el
castillo, Lakitu y el mapa inferior fueron visibles. El control del personaje,
la ejecución de toda la introducción y el rendimiento físico siguen pendientes.

La adquisición `1791616229939712000` (ELF `22a49e1552a15a6972cc0db302222ebd91ce58548cd0f0ec286beaff0708dde0`)
conserva RAM de ticks 180, 2810 y 2970. Se consumió un toque en 2337; el título
quedó listo en 2810 y se observaron logo, fondos y Mario. El límite terminó
la ejecución después de la primera A en 3036. El replay de la RAM lista
ejecuta dos A, elige una ranura vacía, llama el `StartFile(1,0)` original y
pide escena 3. El replay completa el fade y la limpieza originales del título;
el candidato `396ac3ff…` despacha hasta `Stage::BeforeInitResources` mediante
su factory y los PMF originales. La ejecución completa de Stage sigue pendiente.
`build/native-integration-check/acquisition-622993.json` verifica
los tres dumps completos, 30 archivos archivados y su tabla de secciones ELF.

El cache de scanout compara los 676.020 bytes físicos relevantes, incluidos
registros de composición, VRAM, paletas y OAM. Una coincidencia completa omite
recomposición y usa una revisión de salida para evitar los tres CRC de pantalla
(786.432 bytes) en el renderer. Cambios de input conservan el camino normal,
y la captura GPU mantiene su ruta independiente. Los tests comparan imágenes
completas y canarios; en esta adquisición se observaron 2.436 aciertos antes
de que la animación hiciera cambiar OAM. Los tiempos son de Azahar y no prueban
60 FPS físicos. Informes: `build/native-performance-plan/scanout-cache-verification.json`,
`scanout-cache-sanitizer-verification.json` y `screen-bindings-test.json`.

La adquisición anterior `1791613748756720000` conserva dos snapshots reales
de 98.116.880 bytes, incluidos el heap raíz, las 226 secciones originales y
OAM. La RAM confirma una espera original por toque de la estrella y dos
causas distintas para los fondos negros: fundido mantenido de engine B y
widgets/rectángulos GX negros de engine A. El replay de las copias reproduce
el avance con un toque controlado, sin reiniciar el juego. El ratón automático
no entregó toques en la adquisición; B sí llegó. No se atribuye una transición
de título o menú a esa ejecución. Procedimiento, contratos y evidencia:
[NATIVE_RAM_DIAGNOSTICS.md](NATIVE_RAM_DIAGNOSTICS.md).

La continuación `1791611477172386000` integra LenVec3 y completó 360 ciclos
originales en Azahar Old durante una ejecución acotada sin fallo de memoria,
excepción CPU ni STOP. Su 3DSX tiene SHA256
`6252b66cc3018b7fc7adcd43070824e1171664f2db2d93b39d901d02693a5888` y su ELF
`62cbc16c07a173da5b6655a33fbc79fab23cee7171e1059458e83d10521ef044`.
La observación de la ventana confirmó estrella animada y «Touch to Start»
legible, con la pantalla superior negra. El intento conserva por separado
`visual-observation.json`; el supervisor no convierte automáticamente los
logs de dibujo en evidencia visual. El menú, nueva partida y movimiento
siguen pendientes. La pulsación de A y el toque coincidente con el cierre
no acreditan una transición ni un fallo del puente de entrada.

El intento anterior `1791610976045409000` presentó el primer stream de escena
1 tras Boot (78 triángulos), pero alcanzó una lectura `0x84` en
`func_02050c14`: una llamada SEQARC directa con raíz de audio nula. El bloque
relacionado ahora conserva la política de audio desactivado en
`func_020518dc`, `func_02051f1c` y `func_02051e60`, devolviendo fallo SDK sin
mutar la raíz ni crear handles. El reproducer ROM/ARM11 conserva ocho
controles negativos del fallo, seis registros SEQARC vacíos reales, 33 casos
de callers y tres casos del caller con seis argumentos. El último intento
registra seis llamadas diferidas de audio al llegar al título.

La misma ruta original alcanzaba las primitivas de vector corto
`func_02053644` y `func_02053380`. Sus proveedores nativos preservan la raíz
entera, los productos con wrapping y el orden de lectura/escritura cuando
origen y destino se solapan. Pasan 2.335 comparaciones de longitud, 14.010 de
normalización y 146 del caller con alias; no se mapea MMIO DS en la prueba
nativa. El enlace general y la identidad de los proveedores se conservan en
`build/native-integration-check/final-provider-check.json`. Los reportes
están en `build/native-short-vector/report.json` y
`build/native-runtime/audio-deferred-test.json`.

La escena 1 es `dScDSMT`, en OV007. `dScTitle` de OV003 es el selector de
depuración. «Touch to Start» exige estado principal/subestado cero, opacidad
`0x1000` y entrada desbloqueada; admite un toque nuevo o B nuevo. A pertenece
a otra ruta de navegación. Los nuevos checkpoints diagnósticos conservan
GX/texturas/paletas antes de decode y scanout/registros/VRAM después de
presentación exitosa, sin seleccionar escenas ni introducir entradas.

En Azahar con perfil Old, el ejecutable restauró los datos generales, completó
los inicializadores principales y recorrió los 120 ciclos originales de Boot.
El intento `1791593448151098000` permitió observar el logo y copyright de Boot
completos, legibles y orientados correctamente. No se verificó el menú.

El intento integrado `1791596592223444000`, ejecutable SHA256
`4b648392166a166599ba860b83fc5c8717fcb9b270ca568efca8ad1bf7352bc9`, completó
Boot y pidió la escena 1 sin accesos de memoria inválidos ni excepción CPU en
Azahar. La aplicación rechazó un material TEXGEN2 de la estrella original del
título, `TEXIMAGE=95230000 / PLTT=3b0`. Las texturas comprimidas de ese material
resuelven y el paquete termina sin parámetros pendientes. El dump conserva
1.466 entradas GX, texturas/paletas y metadatos para reproducir el problema en
host. No hubo frame presentado de título, captura del juego ni partida. La
ventana observada después del fallo fue la lista de títulos de Azahar.

La corrección integrada abarca el bloque completo de partículas: diez fuentes
adicionales, incluyendo sus seis callbacks y dos helpers de quads, más los
proveedores de Toon y matriz. Sesenta fuentes fijadas contienen 496 sitios
MMIO adaptados. Los dumps del primer stream, texturas, paletas y metadatos se
escriben antes del decode; un rechazo conserva material para replay de host.
El diagnóstico distingue comando desconocido, texgen, resolución de textura
y paquetes pendientes. El intento citado supera las E/S sin adaptar de este
bloque. A partir del snapshot se implementó TEXGEN2 con la normal original,
una matriz entera y el redondeo del manual NITRO; STORE/RESTORE 0 en modo de
textura conserva su propio slot sin alterar el de posición. El replay real
ahora acepta las 1.466 entradas y sus 78 triángulos, con 180 UV generadas
exactamente iguales a una referencia entera independiente. Esta aceptación
de host todavía no acredita un frame presentado del juego.

El mismo stream usa dos proyecciones: seis triángulos con la primera y 72 con
la final. El renderer anterior aplicaba la final a todos. Cada triángulo ahora
conserva su matriz, y la clave y el dibujo de cada batch la utilizan para las
pantallas y la captura. Los 78 campos coinciden con observadores de sus tres
vértices; ningún triángulo del snapshot mezcla matrices. Los materiales ya
creaban cinco batches y el cambio conserva esa cantidad. Una fixture con
materiales iguales demuestra que un cambio de matriz sí separa los batches.

La ruta original de repetición de direcciones del menú alcanzaba
`cstd::mod` al mantener una dirección durante al menos 20 ciclos. Su proveedor
nativo calcula el resto firmado sin registros DS y conserva los casos de
divisor cero y overflow. Pasan 593 comparaciones ROM/ARM11 y 24 recorridos del
llamador real con el quinto argumento en pila; el baseline reproduce cuatro
accesos inválidos al divisor DS. Esto verifica el contrato, no navegación UI.
Cada resultado está en `build/native-runtime-azahar/latest.json` y en su directorio
inmutable `attempt-*`. Los accesos inválidos cortan inmediatamente el emulador,
para evitar que un fallo genere millones de líneas durante el cierre.

Las escenas Boot, título y el bloque inicial OV007 se generan a partir del
fuente fijado, cambiando accesos MMIO enumerados y una reparación ABI probada
contra la ROM. Esta última recupera el argumento de modo de `bd648` perdido
por la declaración de `b6eb4`. Cada transformación comprueba la inversión
exacta, el hash del fuente y las cantidades de cambios esperadas.
La transición de Boot selecciona explícitamente el inicializador OV000 en la
dirección compartida `0x020aa420`; el nombre OV001 del llamador recuperado era
ambiguo y se refería a otro cuerpo. Se conserva la creación de los 2058 handles
por la función original y se compara con el catálogo verificado.

En host, los 370 miembros de los 13 NARC originales coinciden en longitud y
CRC32 con `ndspy` (1.307.658 bytes descomprimidos). Resultado:
`build/game-data/all-narc-verification.json`. También pasó el test existente de
GameFiles con el paquete EU, bajo sanitizadores. Esto valida recursos, no una
partida.

Pruebas nuevas y alcance:

| Prueba | Evidencia | Alcance |
|---|---|---|
| Assets de Boot/menú | `build/native-runtime/boot-assets-test.json`, `menu-assets-test.json` | LZ16 ARM11, streams/Nitro byte a byte, reubicaciones y ABI de modo |
| Publicación de globals | `build/native-runtime/menu-callbacks-test.json` | Cargador e inicializador reales ARM11, ocho callbacks/defaults y constantes |
| Matemáticas del menú | `build/native-menu-math/report.json` | 5.460 comparaciones de hojas ROM/ARM11 y 849 snapshots aritméticos |
| Constructores de matrices | `build/native-identities/report.json` | 7.024 comparaciones; seis hojas con bytes idénticos a la ROM |
| Bancos de texturas | `build/native-runtime/texture-bank-test.json` | Transferencias y reasignación de bancos con las tablas EU originales |
| Compositor y tap de captura | `build/native-runtime/native-2d-test.json` | 364 comprobaciones de host, ASan/UBSan, compilación ARM11 |
| Contrato de captura VRAM | `build/native-capture/report.json` | 128 combinaciones de banco/tamaño/offset/fuente A y casos de rechazo en host |
| Captura GPU | `build/native-capture-gpu-azahar/latest.json` | Dos fixtures sintéticas completas en PICA emulada; cero diferencias de bytes |
| Perfil original | `build/native-personal/report.json` | 1.005 exportaciones ROM/ARM11, 132 colas del llamador, 79 casos host con sanitizadores |
| Cámara y proyección | `build/native-runtime/matrix-submit-test.json` | 850 comparaciones LookAt, 144 de proyección y 20 streams completos ROM/ARM11 |
| Constructores OV007 | `build/native-runtime/menu-constructors-test.json` | 48 comparaciones del ELF integrado frente a ROM, datos/ABI/loans y control negativo |
| Uploads/matrices/partículas | `build/native-runtime/render-upload-test.json` | 580 comparaciones ROM/ARM11, seis callbacks reales y replay de 104 streams en host bajo sanitizadores |
| Parser y bindings SPA | `build/native-runtime/particle-assets-test.json`, `menu-texture-binding-test.json` | Cinco etapas ROM/ELF y 512 píxeles originales comparados en host |
| Luces de polígonos | `build/native-gx-audit/light-mask-test.json` | Los bits de luces de POLYGON_ATTR producen 12 colores esperados; baseline falla y corrección pasa |
| TEXGEN2 y slot de textura | `build/native-gx-audit/texgen2-test.json` | 442 casos, 1.134 UV de fixtures y 180 UV del stream real bajo ASan/UBSan; retirar solo el rechazo sigue fallando |
| Proyección de cada batch | `build/native-runtime/game-projection-test.json`, `gx-projection-audit.json` | 9.284 comprobaciones host, 2.131 comparaciones de clip y los 78 campos de proyección contrastados con observadores |
| Repetición de direcciones | `build/native-runtime/menu-repeat-test.json` | 593 contratos de resto ROM/ARM11, 24 callers reales y cuatro controles negativos del baseline |

La comparación host del compositor obtuvo medianas 1,676958 → 0,695542 ms
con 40 muestras. El diagnóstico de Boot en Azahar obtuvo medianas emuladas
211,751 → 105,748 ms de scanout, siete muestras por binario, registradas en
`build/native-runtime/boot-scanout-timing.json`. No son medidas de rendimiento
físico ni de gameplay. Los tiempos decode/scanout/submit describen fases
separadas del frame; el readback de captura pertenece a submit.

Pendientes: verificación integrada de selección de archivo y nueva partida,
estado completo de carga/descarga de overlays, servicios SDK restantes,
composición visual, cámara, audio y guardado probado. No hay prueba en consola
física. No se ha demostrado 60 FPS estables ni velocidad correcta en Old 3DS.


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
