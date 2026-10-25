## Current local test candidate — BUILD 24, f79b2d5aaa2ef29b (2026-10-25)

The owner's Luma crash `00000005` is a data abort at `Heap::Allocate`, with
`r0 = FAR = 0`. Its complete 96-byte code window matches the previous
`2c182ba59513289f` ELF. On a first install, `enhance::boot_apply()` reads
settings before the retail heap exists; missing `settings.ini` made
`integrated_storage::open_read()` construct a default-allocator `std::string`
for `.bak`, calling the game's global `new` through a null default heap.
Existing settings bypassed that allocation, masking the bug in prior tests.

Recovery and replacement now construct bounded sibling paths on the stack.
Backup-first commits and rollback are preserved; overlong paths fail before
a write. The new first-boot regression forbids all C++ allocation while
exercising absent, existing and backup-only settings plus writes in both
model configurations. It fails with the frozen old helper and passes with
the fix under ASan/UBSan. Storage fault injection, audio/cart/menu regressions,
ARM compilation and the complete integrated build/audit gates pass.

The build pipeline reserves an increasing number in `build_version.hpp`
before freezing inputs. Numbering starts after the 22 distinct closed
historical ELF candidates; failed/interrupted reservations leave gaps (23
was rejected by the host UI gate, 24 is the first delivered numbered build).
The same BUILD label is baked into the title, pause root, native options,
diagnostics, Update page, boot log and package receipt. The small 5x7 badge
occupies the bottom-left strip below the root's last button; existing retail
menu glyphs and positions are preserved. UI images remain cached.

Azahar 2126.2: the old CIA reproduces the missing-settings failure. BUILD 24
passes that point on Old (80 MiB) and New (124 MiB, core 2); the directed
`bob-mario` fixture then loads the original level, accepts real dialogue
input, and opens pause with Start, showing BUILD 24 on both models. New's
Audio page saves volume 135 to the isolated SD; Old's backup-only restart
loads those settings successfully. Screenshots and private logs are under
`build/crash-fix-00000005/`. These are functional checks, not FPS or physical
console proof. No personal saves, ROM or vendor source were changed.

**Normal title check:** the initial black screen with a star is the original
interactive introduction (substate 11), not a deadlock. Read-only local GDB
inspection found its animation intentionally paused at frame 0 with input
locked until the star is touched inside DS coordinates x=96..160, y=64..128.
Touching the text below it does not satisfy that original hit test. A real
touch on the star advances the intro and shows the complete title and native
front options with BUILD 24; Adventure then reaches the normal file selector.
No RAM patch or title bypass was used for this
check. The earlier apparent title-block conclusion was withdrawn.

Physical Old/New installation and sustained performance still need console
validation; these directed checks are not a complete playability release.

## Previous local test candidate — 2c182ba59513289f (2026-10-24)

Build/link, host tests and ELF/3DSX/CIA audits pass. Validated in Azahar only
(Old and New models); console installation and physical performance remain
unverified. Nothing was published.

**Pixel-perfect interface.** Backgrounds, the lower map and the 3D keep filling
the screens; only the 2D interface is drawn 1:1. Lower screen: sprites in front
of every background pixel (lives, stars, map icons) leave the DS OBJ layer and
are drawn at 1x over the stretched panel, grouped when they touch and placed
proportionally (`sprite_layout.hpp`, `native_sprite_overlay.hpp`). Upper screen:
each 2D element (pause box, texts, counters, controller diagram) is composed
1:1 in compositor tiles over the native 3D (`ui_layout.hpp`). Unsupported
states (windows, semi-transparent OBJ, captures, menus) keep the stretched
layout. The unused repainted HUD was removed.

**3D at native resolution.** Raising the 3D slider used to drop to DS
resolution (about 1.25 MiB VRAM short) and stay there after lowering it. The
right eye now shares the left eye's depth buffer (command list split before its
clear) and the capture/semi-OBJ scratch pool is released while native stereo is
shown; a refusal only applies to the eye count that failed. Azahar Old: native
400x240 stereo with 1 MiB VRAM free, back to native mono and native stereo again
at runtime, pause menu correct in both eyes.

**Level loading.** Every game file used to open the `.sm64ds` package on the SD
again (about 25 ms each in Azahar; a level reads ~75 files). One shared FS
handle now reads members at their offset. While the star select plays, the
prefetch thread reads the chosen level's files into RAM (`level_files.hpp`:
lists recorded per level in `sdmc:/3ds/sm64ds/level-files.bin`, built-in lists
for the measured courses). Azahar Old, Bob-omb Battlefield load: 2.4 s →
0.43 s (shared handle) → 0.27 s (preloaded, 73 of 73 files from RAM). The
boot-time player animation prefetch went from 7.3 s to 0.38 s. Each frame
longer than 250 ms logs a `Long frame` line with its file costs.

**Old / New 3DS profiles** (`console_profile.hpp`). Old: 80 MB, 268 MHz,
worker on core 1, existing budgets. New: exheader 124 MB, 804 MHz, L2 cache and
core 2; presentation worker on core 2 with the PPU helper on core 1, GPU texture
cache 12 MiB, decoded texture cache 16 MiB, level preload 8 MiB. A New 3DS in a
smaller memory mode (3DSX) keeps the Old budgets; without core 2 access the
worker falls back to core 1. Both profiles booted and loaded Bob-omb in Azahar
from the CXI.

**Bob-omb at 30 FPS (open).** The automatic benchmark stops on the course's
intro dialogue: every pose is identical, so Render60 has nothing to interpolate
and the counter reads 30. Real play with Mario moving still needs measuring.

## Previous local test candidate — 6788c4dd9ed6898b (2026-10-23)

Build/link and ELF/3DSX/CIA audits pass. Local CIA and3DSX delivery:
`dist/sm64ds-rom-only-6788c4dd9ed6898b/`; package contains no ROM, DSP firmware,
save or dump. Nothing was published. CIA installation/boot and physical
performance remain unverified.

Old-model Azahar completed3timed v2 process/LCD/VRAM bundles in97.35s with no
recorded emulator fault or automatic hitch captures at the default30lock.
Six additional bundles on the preceding c2451eea binary validated Display and
the30/60/Off controls. Readers reject incomplete VRAM and invalid LCD geometry
or mismatched context; see [diagnostic evidence and limits](NATIVE_RAM_DIAGNOSTICS.md).
`WorkerMesh` includes presentation synchronization; do not treat its entire
duration as geometry CPU cost. Per-function profiling, full routes and the
stable60FPS physical target remain open.

## Closed-lid preparation and packed assets — 14efb42f4a1b16ce

The preparation phase owns a temporary sleep guard, suppresses screen rendering
when the shell is closed, and requests a green notification blink only after
files and audio finish successfully. It waits for reopening before game startup.
Prior sleep policy and owned service references are restored on exit. Physical
Old/New 3DS lid, backlight and LED behavior remains unverified.

The portable parser and read-only newlib device read a validated `.sm64ds`
container directly without extracting its 2,450 members. Host ASan/UBSan tests
compare all 15,686,745 payload bytes against canonical files, exercise independent
streams and seek boundaries, reject damaged/truncated/trailing/schema-mismatched
containers, and use no game/global heap before initialization. ARM11 adapter
compiled with warnings treated as errors; final ELF and CIA audit passed.

One exact CIA acquisition on Azahar Old completed in 175.53 seconds with no CPU
fault. It reached the original title/menu using ONLY `Mario.sm64ds`, no `.nds`
or cache in the profile. Runtime log: root `s64:`, written=0, reused=2450,
bootLive=0/bootBlocks=0 after preparation, tracked boot allocation peak=64 bytes.
This counter is not a complete game/process leak audit. Audio was muted and the
emulator's existing DSP component was used. Gameplay and physical acceptance
remain distinct from this packed-file boot proof.

Evidence: `build/integrated-native/acquisitions/attempt-1792131049700333000/`;
`build/integrated-native/desktop-packer/loader-report.json`;
`build/integrated-native/setup-power/receipt.json`.

## Boot UI and SD directory update — 9442f1d423f6ff23

The approved original menu artwork is embedded as 329,344 bytes of constant
pixels, with zero artwork heap allocation at boot. It remains visible when
no ROM is readable. The logo is centered in both axes on the top screen.
All current runtime paths use `sdmc:/3ds/sm64ds/`, including discovery,
cache, saves, settings, logs and optional captures. Any `.nds` filename is
accepted; the verified EU revision requirement remains.

Host ASan/UBSan gates passed for six languages (7,356 glyph checks), pixel
comparison of embedded graphics against the independently decoded reference,
ROM discovery and DSP bootstrap. ARM build/link and the pre-emulator audit
passed. This exact build has not yet been exercised in Azahar or on hardware.
Earlier emulator proofs apply to their respective earlier binaries.

# Estado comprobado — 15 de octubre de 2026

La revisión `9951be9b86f38939` descubre cualquier archivo `.nds` directamente
en `SD:/sm64ds/`, incluidos nombres con espacios, Unicode y extensión en
mayúsculas. Comprueba cabecera y contenido; si una copia candidata está dañada,
continúa con la siguiente antes de escribir la caché. El escáner usa memoria
constante y pasa 12.970 comprobaciones con ASan/UBSan, extracción íntegra y
reutilización tras renombrar la ROM. Su CIA se verifica contra el ELF exacto.

El antecedente integrado ROM-only `2f965be15233cab0` compila y su CIA se verifica
contra el ELF exacto. Prepara los recursos desde `SD:/sm64ds/rom.nds`, sin árbol
extraído ni firmware incluido en el paquete. La pantalla aprobada usa el
castillo, el logo, el patrón, los marcos y las fuentes originales de esa ROM.
Selecciona inglés, español, portugués, francés, italiano o alemán desde CFGU;
los demás idiomas y los fallos de consulta seleccionan inglés.

El gate de la pantalla pasa 17 casos de selección y referencias CFGU,
300 cadenas, 7.278 comprobaciones de glifos y 90 estados en los seis idiomas,
además de ASan/UBSan, texto largo y UTF-8 malformado. El decodificador compara
los ocho recursos con el oráculo Python y pasa 2.518 comprobaciones. Sus
asignaciones de arranque retienen 329.344 bytes y alcanzan un pico de
772.685 bytes, con cero bytes propios tras liberarlos. La primera preparación
del CIA en Azahar Old ya comparó íntegramente los 2.450 archivos, 15.686.745
bytes, con la referencia. La ROM se conserva y no quedan archivos `.part`.
El registro antes del juego y la RAM de escena 3 comprueban cero bytes y cero
bloques del asignador de arranque pendientes. La captura nativa valida además
los 11.204 bytes de guiones, 61 punteros y 392 escrituras originales. Se
observaron título, selección de archivo e introducción originales, además de
Yoshi controlable con desplazamiento y salto. La adquisición cerró tras
736,07 segundos sin fallo emulado de CPU/memoria registrado. La lectura DSP
del sistema físico y los 60 FPS sostenidos siguen pendientes. Las instrucciones
están en [ROM_ONLY_INSTALL.md](ROM_ONLY_INSTALL.md).

Se integró la plataforma nativa de Sébastien Breton como target separado
`sm64ds-integrated_3dsx`, con fuentes públicas MIT conservadas y revisiones
fijadas en `reference-native.lock.json`. La compilación completa y la auditoría
previa a emulación pasan: 11.204 bytes de guiones, 61 punteros auténticos,
64 comandos de la introducción, seis palabras serializadas intactas,
proveedores de juego/cámara/audio enlazados y empaquetado ELF/3DSX exacto.
El gate host de controles pasa con ASan/UBSan y el candidato selecciona
Circle Pad analógico y cámara libre. En Azahar Old ya se verificaron la
introducción y Yoshi controlable, con movimiento y salto, en el candidato de
referencia. El candidato experimental `a59f000b1b7106b1` completó una adquisición
de 535 segundos en Azahar Old sin un fallo de CPU/memoria registrado. Se
observaron la introducción, marcha, nado, transparencias, pausa y reanudación.
Las capturas de interfaz mostraron 52–60 FPS en distintos tramos del exterior,
47–60 en agua y caídas a 19–20 en la introducción; son muestras aisladas y no
una distribución de rendimiento. No cumple todavía 60 FPS sostenidos.
Conserva tres estados completos de Azahar validados, además de los registros.

La revisión `f90d66d5b27311d9` cerró una adquisición de 787 segundos sin fallo
emulado de CPU/memoria registrado. Conserva cuatro capturas nativas completas,
cada una con 114.335.744 bytes virtuales legibles, y tiempos internos en un
búfer fijo de 640 KiB. La inspección comprueba los 11.204 bytes de guiones,
61 punteros y 392 escrituras originales del constructor. La cadencia cambia
de 1 en el título a 2 en Stage. Se verificó movimiento real de Yoshi mediante
su UID y posición en RAM, además de 180 actualizaciones originales durante
una pulsación de seis segundos.

Los tiempos internos identificaron preparación 2D costosa durante la intro.
Se implementó una optimización acotada para alfa sin capas que mezclar:
64 casos reproducidos con RAM capturada pasan comparación exacta de
12.586.816 bytes de salida, con ASan/UBSan. El candidato `bcc1fbc9bf6af826`
pasó compilación ARM y auditoría. Su primera adquisición no consumió entrada:
un diálogo de actualización oculto de Azahar bloqueaba el HID. Tras cerrarlo
con Ignore, la recuperación del mismo binario permitió menú, introducción,
movimiento, salto, pausa y reanudación originales. Se validaron cuatro capturas
nativas más y un estado completo de 184.050.868 bytes descomprimidos. La salida
de Azahar fue observada; el supervisor falló después en su limpieza con EPERM.
El recibo recuperado conserva ese error y confirma que el PID y grupo propios
ya no existen, sin inventar un tiempo de ejecución exacto.

La comparación de la misma ventana de 907 actualizaciones de introducción
no establece una mejora relevante: el coste total de preparación 2D por
actualización cambia de 9,468 a 9,391 ms emulados. La inspección del trabajo
retenido y su OAM, usando DWARF de cada ELF exacto, identifica una causa
concreta: un OBJ semitransparente rechaza la ruta residente antes de preparar
el fondo affine en GPU. El trabajo activo contiene la imagen resuelta por CPU;
la caché affine inspeccionada antes era histórica.

La siguiente corrección integra OBJ semitransparentes en el overlay del fondo
affine, conservando prioridades y mezcla entera y usando el PPU completo para
casos no admitidos. El gate offline pasa 384 variantes admitidas y 50 rechazos
entre ambas RAM; 18.874.368 píxeles coinciden con cada uno de los dos oráculos
CPU originales bajo ASan/UBSan. También corrige la identidad de la textura al
alternar productores con igual generación. El candidato nuevo
`277c9139291b55c4` ya compiló para ARM y pasó la auditoría y el gate de fuentes.
Arrancó con el perfil Old y la adquisición cerró tras 205,15 segundos sin un
fallo CPU/memoria registrado. macOS estaba bloqueado: no se pudo ejercer el
HID ni observar su ventana, y se cerró solo la instancia propia. Ese título
registrado no demuestra la corrección visual ni su rendimiento. La recuperación
queda preparada para una única prueba interactiva del mismo candidato al
desbloquear el Mac; exige comprobar antes que no se consumió ninguna entrada.

La revisión de persistencia añade evidencia independiente sin otra apertura
de Azahar: el HAL de guardado y las transacciones originales del menú pasan
pruebas host con ASan/UBSan (crear, reemplazar, releer, espejo y recuperación,
seis copias dirigidas y tres borrados de ranura). Las RAM jugables f90/bcc
seleccionan archivo B aún no guardado, sin estrellas y sin rechazo por modo
inalámbrico. Es coherente que no exista todavía `.sav`; no demuestra que se
haya completado una escritura dentro del juego. La siguiente adquisición
debe ejercer una acción original de guardar y verificar la recarga.
Véase [INTEGRATED_NATIVE.md](INTEGRATED_NATIVE.md). Ninguno de estos resultados
acredita 60 FPS en una Old 3DS física ni juego completo verificado.

Se conserva el runtime experimental y toda la evidencia anterior. El árbol
SD nuevo y los binarios no sustituyen el CIA histórico ni los saves existentes.
Las siguientes secciones describen los hitos y bloqueos de ese runtime anterior.

El objetivo solicitado **no está completado**. El CIA del visor histórico sigue siendo
la compilación de desarrollo del importador y visor. El target general separado
`sm64ds-native.3dsx` ya alcanza el título y la selección de archivo originales,
pero todavía no completa la carga de una partida controlable.

El criterio final de aceptación es el juego completo funcionando correctamente
a 60 FPS sostenidos en una Old 3DS física. El C-stick debe controlar una cámara
libre y fluida, con giro e inclinación continuos en los modelos que lo tienen.
La velocidad normal del juego debe conservarse. Ese criterio permanece
pendiente; los resultados de Azahar o de replays CPU no lo acreditan.

La continuación más reciente conserva cuatro dumps completos de 98.077.304
bytes de la adquisición C13 `1791638741587754000`. El actor de introducción
UID 85, parámetro `0x2f`, tiene su controlador residente. El fallo posterior
fue `Read32 @ 0x04001000`; el replay del ELF exacto reproduce la instrucción
`0x002760cc` de `func_ov002_020f1fcc` en el último paso del fundido. El PC
`0x002760b4` del registro de Azahar es la cabecera del bloque JIT. La RAM
completa anterior no contiene el PC/GPR live de ese fallo. Evidencia:
`build/native-integration-check/cutscene-fault-C13.json`.

El candidato C14 `candidate-cutscene-video` adapta doce cuerpos OV002 y 54
sitios de registros/SDK, conservando los contadores y transiciones originales.
El gate del ELF de producción pasa 63 comparaciones con ARM9 de la ROM en
once rutinas; la carga de recursos del grupo de créditos (`0x2e`) pasa seis
idiomas con comparación íntegra de vídeo, buffer LZ16 y canarios. No son prueba
de completar la introducción ni los créditos. ELF SHA256
`10cf7a773962137707a27d98abcf227abda59c25c5aeacdced4cd558c08aa6db`.
Una única adquisición integrada C14 `1791640481103529000` está en curso.

La caché de texturas decodificadas C13 conserva versiones inmutables y permite
reutilizar sus identidades entre presentaciones. Su gate host bajo ASan/UBSan
verificó 53 bindings de RAM y 3.727.872 bytes de upload; las fuentes desconocidas
y las versiones nuevas conservan CRC completo. El análisis de los intervalos
emulados separa decode/scanout/submit y CRC de pantallas. No permite concluir
el porcentaje de mejora en Old 3DS física ni extrapolar FPS del emulador.

La adquisición de referencia del bloqueo del registro es `1791624434443135000`, con ELF SHA256
`cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29`
y 3DSX SHA256
`fddec3723c4646fcea5a7d3904c8e3380d078019e2c7d72572553161f1319346`.
El candidato y sus gates quedan congelados en
`build/native-integration-check/candidate-stage-camera-video-power-tls/`.
Las seis piezas de cámara son idénticas a los objetos de sus pruebas ARM;
la comprobación de enlace conserva proveedores únicos y reutiliza los gates
de las primitivas cuyas instrucciones no cambiaron.

En esa adquisición se consumió el toque de la estrella en tick 2792, el título
quedó listo en 3270, llegaron dos pulsaciones nuevas de A en 3312 y 3448,
y el toque de la ranura B nueva se consumió en 3748. Se observaron el título,
los botones derechos y la selección de archivos. El juego solicitó escena 3
en tick 3859. Se guardaron tres dumps completos de 98.093.592 bytes cada uno,
con las 226 secciones originales, heap, pila, vídeo y 512 bytes del TLS real.
El analizador verifica los 30 archivos archivados contra sus hashes y el ELF
exacto: `build/native-integration-check/acquisition-443444.json`.

En esa adquisición, la espera empieza después del checkpoint `ram-stage-requested`,
tomado **antes** de `SpawnIfNecessary`, con el tick abierto. La RAM confirma
fase 3, escena pendiente 3, flag de spawn cero y contexto de título destruido.
No terminó otro frame antes del límite de 360 segundos; no se produjo el
checkpoint posterior `ram-stage-presented`. No hubo STOP ni excepción CPU
explícita observada. Ese límite no identifica el PC del bloqueo ni demuestra
su causa exclusiva. La investigación offline identificó un defecto
concreto del registro de archivos: la cabeza capturada apunta a memoria libre
y el registro ROM permanente está a cero. El replay carga dos NARC
reales; el primer montaje reutiliza aquella dirección y se enlaza consigo
mismo, por lo que la búsqueda del segundo archivo no termina. El arranque
nativo omitía el registro ROM que mantiene esa primera entrada permanente.
Proof y límites: `build/native-integration-check/stage-requested-443444/READOUT.md`
y `CAUSAL-PROOF.json` del mismo directorio.

La corrección registra la ROM mediante las funciones originales antes de
publicar los globals del catálogo, conserva el unregister original y comprueba
la idempotencia sin limpiar la cadena NARC. El nuevo candidato compila y pasa
la revisión estática de enlace. Está congelado en
`build/native-integration-check/candidate-rom-archive-anchor/`, con ELF SHA256
`1e1eef06dff21c7a4ded69c93366f5c3d2718f8190d129abe06ffb94dec78ba0`
y 3DSX SHA256
`bdb25495aa59c260aae5d9186f7e6c57e36324a2de49ed4dd8356d9a1237d39c`.
El gate ARM nuevo de lifecycle pasó 17 casos (276.028 instrucciones), con
montajes reales, errores, retirada, recarga y vida de archivos abiertos.
Informe: `build/native-stage-plan/archive-registry-gate/report.json`.
La adquisición corregida `1791627213451636000` completó 178,034 segundos de
juego y alcanzó una parada explícita nueva: `__sinit_d_a_star_gate.cpp`, durante
la activación original de ov100. Conserva cuatro dumps de 98.089.496 bytes y
su ELF exacto; `build/native-integration-check/acquisition-721345.json` pasó la
auditoría de 32 archivos. La RAM de STOP contiene la lista coherente
ROM → c2d → cee → ar0 → ar1, sin ciclo. No hubo excepción de CPU/memoria
observada ni frame Stage presentado. El intento previo `1791626988742814000`
expiró en el aviso del lanzador antes del primer registro del juego: es un
fallo de arranque conservado, sin adquisición de RAM.
La RAM anterior conserva su ELF: no se instala sobre el layout nuevo.

El siguiente bloque restaura tres entradas de la tabla original de
inicializadores: Battan (ov023), Kb1 billboard (ov044) y Star gate (ov100).
GCC genera nombres locales distintos y se retiran sus listas de inicio de
proceso; faltaba enlazar esas tres entradas después de restaurar ROM/catálogo.
Los proveedores nuevos conservan los constructores de recursos, el orden y
callbacks de limpieza originales y los cinco estados de Star gate. El gate
del ELF final pasó 18 casos y 24 callbacks contra instrucciones ASMP0; incluye
memoria previa con tres patrones, cabeza vacía/con nodo previo, límites de
escritura y ABI. No recompila la producción para la prueba ni importa RAM vieja.
Evidencia: `build/native-integration-check/candidate-overlay-static-init/`, ELF
`bf1052223b47984e23887bb69f0312264408e302e13d0d008d520e2f274b9d5e`.
La adquisición integrada `1791628210782988000` de ese candidato completó
254,353 segundos de juego y alcanzó STOP en `ShadowModel::InitCuboid`, en
tick 3053 durante `SpawnIfNecessary`. Conserva cuatro dumps de 98.089.528
bytes y pasa la auditoría de sus 32 archivos. En RAM se mantienen los cinco
archivos coherentes del registro, se activaron ov100 y los demás módulos del
nivel, y existen Camera/Player con sus vptrs correctos. Son objetos durante
la inicialización: todavía no se presentó Stage ni se demostró control.
Evidencia: `build/native-integration-check/acquisition-821078.json` y
`acquisition-821078-registry.json`. Se corrigieron además las dependencias
de objetos de CMake para regenerar tablas al cambiar proveedores; la
compilación incremental siguiente no las regenera.

`InitCuboid` estaba recuperado upstream, pero fuera de la selección nativa.
La corrección incorpora ese cuerpo y enlaza los dos BMD de sombra con sus
datos reales de ov001: `020ad524` y `020ad560`. El ELF final
`bbde7a4e77fca02ed870501e05ae13d405edfc7d2811c76dbb49f35c80225303`
pasó diez casos ARM contra ASMP0: parsing completo, buffers de 20/148 bytes,
matrices, materiales, caché compartida, lista y destrucción, dos clases de
fallo de asignación y recuperación. GX Begin/End de paletas son límites de
dispositivo declarados; ambas plantillas tienen cero texturas y paletas.
Ese gate no acredita GPU ni frames del juego. Evidencia congelada:
`build/native-integration-check/candidate-shadow-cuboid/ARM-gate.json`.
La adquisición integrada posterior `1791629860058059000` completó 245,629
segundos y alcanzó STOP en `da1up_c_classInit_ONEUPKINOKO`, tick 2663.
Sus cuatro dumps completos pasan la auditoría de 32 archivos. La RAM contiene
60 modelos comunes; las dos sombras tienen componentes y buffers cargados,
materiales `1f8080` y matrices de identidad. El buffer de 148 bytes coincide
con el gate. Evidencia: `acquisition-986005.json` y
`acquisition-986005-shadows.json`, en `build/native-integration-check/`.

La fábrica faltaba porque GCC rechazaba las declaraciones locales con tipos
distintos en la unidad completa de 1-Up/Mega Mushroom. La adaptación mantiene
91 vistas locales con alias al símbolo original, de forma reversible, y
compila las 38 funciones. La prueba aislada detectó además que GCC eliminaba
el control de NULL antes de construir el objeto; `-fcheck-new`, limitado a esa
unidad, conserva el retorno cero que ejecuta ASMP0 ante OOM.
El candidato `candidate-mushroom-nullguard` pasó ocho casos sobre su ELF final:
920 bytes del objeto construido, listas antes/después, dos fábricas, modos
0/2, tres patrones previos, D1/D0, canarios y ABI. Tras la destrucción se
comparan 904 bytes y se verifican separadamente cuatro vptrs sin vida que GCC
conserva y MWCC cambia a la tabla base; se comprueban los 31 destinos virtuales.
Ese gate aislado no ejecuta los recursos del hongo. La adquisición integrada
`1791631629219652000` completó 250,369 segundos y alcanzó STOP en
`daMip_c_classInit`, tick 3224, sin excepción de CPU ni acceso inválido.
Sus cuatro dumps completos de 98.085.432 bytes pasan la auditoría de 32 archivos.
La lista de 61 actores es acíclica y conserva enlaces prev/next/owner coherentes;
los tres hongos tienen estado vivo, tipo 3 y componentes de modelo cargados.
Esto acredita su inicialización real, todavía sin Stage presentado ni control.
Informes: `acquisition-162921.json` y `acquisition-162921-actors.json`.

El bloque de MIPS, `actors/daMip_c.cpp` de ov085, compila sus 32 funciones tras
adaptar 63 vistas locales de datos conservando tipos y símbolos originales.
`candidate-mip-unit/mip-arm-gate.json` pasa diez casos contra ASMP0: construcción
de 1.140 bytes, OOM, animación, matrices/sombras, listas, globales y cleanup de
siete handles vacíos. Cinco vptrs al final de vida se verifican por separado;
los otros 1.120 bytes destruidos coinciden. No se ejecuta I/O ni un frame del juego.
La revisión de las tablas de objetos estándar, entrada, simples, puertas y
salidas en la RAM anterior identifica 72 entradas y 18 fábricas distintas:
MIPS era la única faltante; el candidato incluye las 18. Otras categorías,
spawns procedurales e inicialización completa siguen fuera de ese inventario.
Informe: `acquisition-162921-level-factories.json`. Su adquisición integrada
`1791633566550586000` completó 179,318 segundos y tres dumps completos de
98.077.240 bytes; una cuarta escritura se interrumpió y queda excluida de
la lectura de RAM, conservando sus hashes. Tras elegir un archivo nuevo,
la carga de Stage falló con Read8 de `0xfd6ed0d9`, antes de presentar el nivel.
El replay del ELF/RAM exactos reprodujo esa dirección en `func_0204a17c+0x88`.
La SPA embebida difería de ASMP0 en dos words: flags de las definiciones 306/307
que la metadata confundía con punteros. Cambiar sus strides desplazaba el
cursor de texturas hasta el acceso inválido. `particle-fault-review.json`
conserva la demostración completa; no hay registros live del instante del fallo.

El generador excluye ahora exactamente esos dos flags de las relocaciones.
El candidato `candidate-particle-flags` (ELF `daf2f6fe…`) pasa la verificación
del empaquetado y el gate ARM de la SPA completa: 49.684 bytes publicados
idénticos a ASMP0, 321 definiciones, 280 behaviors y 46 texturas. El constructor,
allocator original y parser, además de los callers de subida con servicios
observados, producen la misma arena y los mismos uploads en ambos modos.
Quedan 140/76 bytes libres. Su única adquisición integrada, `1791635512435132000`,
duró 212,803 segundos y produjo cuatro dumps completos de 98.077.240 bytes.
La RAM conserva la SPA completa y confirma todos los registros y punteros del
manager; su arena live tiene 140 bytes libres. La lista de actores contiene 90
nodos con enlaces coherentes, incluidos tres Mush y un MIP vivo. El siguiente
fallo fue Write32 de 2 a `0x04000440`, dentro de `func_02044534` al renderizar
billboards. El checkpoint llamado `stage-presented` describe una petición de
scene 3 tras una presentación anterior; no acredita un frame visible de Stage.

El candidato `candidate-model-billboard` (ELF `cbdb84c3…`) corrige los siete
accesos GX de esa rutina, conserva las tres escalas y su orden, y sustituye su
única transferencia SDK con callback/arg nulos y espera inmediata por el FIFO
nativo ya usado por la rama hermana. El gate compara 13 casos completos con
ARM9 original: matrices, comandos, transportes, espera y entradas/canarios.
El empaquetado exacto pasa; tablas y manifest de runtime, seis objetos de cámara
y once providers previamente comprobados se conservan. Su adquisición integrada
`1791636898304174000` terminó por límite tras 360,030 segundos de juego, sin
fallos de acceso a memoria de CPU registrados. Los cinco dumps completos de
98.077.240 bytes y 41 archivos conservados pasan la auditoría. La SPA conserva
sus bytes y los 2.533 campos de puntero revisados en ambos managers capturados;
las listas de actores permanecen coherentes tras 31 destrucciones pendientes.
Se observaron la carta de Peach, el castillo, Lakitu y el mapa inferior. Esto
acredita la introducción visible del escenario; aún no acredita control del
personaje ni rendimiento físico. Se retiene aparte un lanzamiento que agotó
el tiempo de startup mientras otra ventana vacía impedía seleccionar el aviso;
no contiene un tick ni captura del juego.
El supervisor verifica ahora que el 3DSX procede exactamente del ELF archivado
antes de iniciar una adquisición; la misma comprobación pasó para los tres
intentos anteriores, sin volver a ejecutarlos.

La evidencia histórica anterior corresponde al **target general separado**,
`sm64ds-native.3dsx`, con arranque/bucle originales, datos de todos los overlays
y servicios nativos de pantalla, archivos, captura y guardado. La adquisición
`1791616229939712000` en Azahar Old completó 180,193 segundos de juego sin
fallo de memoria/CPU ni STOP observado. Un toque normal se consumió en tick
2337 y el título original quedó listo en 2810: estado/subestado cero, entrada
desbloqueada y opacidad completa. Se observaron el logo superior, los fondos
azules y Mario en la pantalla inferior. La inversión de los botones se
localizó en las UV del renderer. La corrección pasó con el helper y uploads
reales, las 24 texturas y los 6822 vértices capturados, bajo ASan/UBSan; su
verificación GPU integrada estaba pendiente en aquel intento.

Se conservaron tres dumps completos de 98.117.260 bytes (ticks 180, 2810 y
2970). La comprobación verifica todos sus bytes por hash, las 226 secciones
contra el ELF exacto y la cobertura de heap raíz, heaps de plataforma, pila,
registros, VRAM, paletas y OAM. El replay desde la RAM lista ejecuta dos A
nuevas, selección de archivo y selección de ranura vacía; el `Behavior`
original llama `StartFile(1,0)` y solicita escena 3. Esto todavía es evidencia
CPU aislada: el fader y la destrucción del menú ya alcanzan el constructor de
Stage, pero faltan su carga completa y una partida controlable en el target.
Un ajuste local de la tabla virtual manual corrige su primer salto a RTTI y
permite entrar en `InitResources` original. La revisión posterior clasifica
411 asignaciones manuales: 351 referencias cuyo prefijo GCC está demostrado,
41 ya ajustadas, cuatro tablas ROM bare y 15 símbolos no retenidos. La receta
de producción repara las 351 referencias y conserva una reparación explícita
previa del store transitorio dScene de Stage: 352 expresiones exactas en 287
fuentes. Desplaza ocho bytes incluso cuando la declaración es `char[]` o un
símbolo escalar; mantiene los aliases ROM y constructores reales del compilador.
Compilan las nueve funciones gráficas de Stage adaptadas y el renderer
corregido. La regresión ARM de las seis cargas originales de gráficos y
paletas pasa en orden, comparando toda la memoria de vídeo tras cada carga.
También corrige el peek de cabecera posterior a `FS_ReadFile`, que conservaba
su dirección DS. Esas fuentes aún no se habían lanzado al cerrar aquel bloque.
En la adquisición real sólo se consumió la
primera A, en 3036; el límite temporal impidió enviar la segunda. Diagnóstico,
evidencia histórica de los fondos negros y límites:
[NATIVE_RAM_DIAGNOSTICS.md](NATIVE_RAM_DIAGNOSTICS.md).
El ELF general ya incluye LenVec3, TEXGEN2, proyección por batch y resto
firmado. También incluye dos primitivas de vector corto, comparadas con la
ROM sin MMIO DS, y una frontera explícita de audio desactivado para llamadas
SDK directas del menú/Player. Esta frontera devuelve fallo, mantiene la raíz
de audio nula y no inventa handles.
La Camera necesita además el cuerpo original `func_02009e70`, que estaba
fuera de delinks: ya está enrolado y enlazado para ARM11 sin cambiar su lógica.
La comparación soft-float y la conversión original de entero a float pasan
13.600 y 8.445 casos frente a la ROM, incluidos estado FP y ABI. El fixture de
Stage parcial del castillo ejecuta el update original, con nueve consultas
Lin/Gnd reales en cada llamada activa y sin cambiar Player ni controles.
La revisión del vptr demuestra un límite del InitResources de ese fixture
histórico: la ranura final no alcanzaba su Camera::Render original. La ROM
confirma el despacho por +0x24 hacia Render; su cuerpo completo ya está
adaptado y la prueba de integración de cámara adopta además LookAt nativo.
El fixture histórico no acredita una cámara manual integrada ni el Stage
completo. El gate posterior de cámara y el nuevo ELF se describen más abajo.
La última adquisición ya incluye el bloque TLS real del hilo para permitir
replays de biblioteca con su contexto capturado. El lector acepta ese estado
sólo con disponibilidad explícita, magic y puntero de reentrancia capturados.

El candidato congelado de este bloque está en
`build/native-integration-check/candidate-vptr-file-camera-tls/`: ELF SHA256
`396ac3ff7f20af01d411357d97da9cdc4dccdee70f6fc53d1d8305a5804361c8`
y 3DSX SHA256
`554844c42674f53b0ccaed7a8e8a393569bb89780b70344e03d3e1b7e02cc65b`.
Es una compilación sin nueva ejecución integrada. Las once primitivas ya
verificadas conservan sus instrucciones y un solo proveedor en ese ELF;
archivo, matemática nueva y Camera tienen proveedores únicos. Continúa la
revisión de las dependencias de carga de Stage. Las once factories del
candidato pasan: siete tablas GCC con address point +8 y cuatro tablas ROM
bare sin desplazar. Sus métodos de recursos completos no están ejecutados
por ese gate. Las 287 fuentes de la receta compilan después de corregir tres
casts de callbacks ARM32; el corpus total mantiene 84 fallos ajenos pendientes.

Las correcciones posteriores al candidato congelado incluyen HUD, Minimap,
Camera::Render, SetFog/SetFogTable y el nombre antiguo del caller de
Player::CanPause. HUD/Minimap pasan en ARM con los recursos reales: doce
variantes del HUD, tres del minimapa y cuatro updates de entrada/temporizador,
comparando los 684.032 bytes de vídeo completos. El fixture declara su
frontera LoadFile y no repite ni acredita FS/Stage/GPU. La niebla se sube aun
desactivada; sus cinco accesos de registro y el destino de 32 bytes requieren
la adaptación nativa. Pasan 48 casos de registros de niebla, 16 tablas y 28
consultas reales de pausa del jugador frente a la ROM. La igualdad de registros
no acredita la representación GPU de niebla. La auditoría estática de 20 cuerpos
Stage/Camera/HUD/Minimap resuelve sus siete llamadas virtuales y conserva los
120 slots GCC y 30 slots de tablas bare, sin diferencias de destino.

El bloque generado de vídeo compila 85 fuentes y 680 accesos de registro.
Incluye seis cuerpos adicionales de ventanas y mensajes y dos de encendido de
LCD. La función LCD usa ahora el contador de VBlank de Citro3D, separado del
reloj lógico de cámara y del temporizador OS. Las transacciones PXI y suspensión
original que requieren ARM7 siguen siendo fronteras pendientes, sin respuesta
inventada. Se agregó el proveedor nativo de IRQ::SetIRQs. Su verificación ARM
pasa 72 casos LCD, cuatro wrappers y 32 cambios de máscara IRQ, comparando
todo el estado de vídeo y plataforma pertinente. La observación de callbacks
Citro3D acredita su contrato CPU; no acredita señales GSP físicas ni suspensión.

La revisión de mensajes localizó un tercer callback fuera de delinks. La ROM
llama dos veces al generador de glifos VS `func_02031b84`; la fuente recuperada
nombraba el generador de otro buffer. Se enroló ese cuerpo con las dos llamadas
corregidas, manteniendo tabla, argumentos y conversión de byte. El corpus pasa
a 8.790 fuentes compiladas de 8.874 seleccionadas; conserva 84 fallos ajenos.

La cámara manual conserva las consultas del castillo original y el objetivo
de radio cuando una pared obliga a acortarlo. El precheck de pared necesita
el bloque automático original completo: omitirlo dejaba una posición detrás
de la pared. El fallback mantiene ese bloque y conserva el radio deseado. El
gate final pasa 134 casos, 1.122 consultas reales de colisión y cinco casos
del bloque exacto de altura, incluidos intervalos imposibles que devuelven
el control a la lógica original. El reloj consume cada publicación de input,
también durante pausas que omiten el cuerpo de cámara, sin acumular tiempo.
El grupo está adoptado en CMake y en el ELF de la última adquisición. El
checkpoint previo a Stage se produjo; el posterior a presentación no llegó.
La cámara manual todavía no se ha observado en una partida integrada ni con
C-stick físico. Informes: `build/native-camera-plan/camera-final-gate.json`
y `CSTICK-PROVIDER-GATE.md` del mismo directorio.
Después de la introducción, A usa el foco de teclado: una pulsación lo arma y
la siguiente confirma Adventure si sigue en el elemento cero. B elige la
ruta de dibujo, que no equivale a nueva partida. No hay prueba integrada de
partida, movimiento ni rendimiento físico. Estado y evidencia de esta ruta:
[NATIVE_RUNTIME.md](NATIVE_RUNTIME.md). La tabla siguiente describe el CIA del
visor anterior, que no se ha reemplazado por esta integración.

| Criterio | Estado y evidencia |
|---|---|
| CIA instalable | Estructura, hashes, perfil Old y permisos verificados; instalado en SD/NAND aislada de Azahar |
| Carpeta `sm64ds` | `/3ds/sm64ds/rom.nds`; extracción a `assets.pak` en la misma carpeta |
| ROM aportada | USA ASME0; 1.940 entradas verificadas; archivo original sin cambios |
| Código nativo original activo | Heap raíz de 4 MiB; prueba real de asignación/escritura/liberación en el arranque |
| Datos del juego | Constantes, 1.824 handles USA, descriptor del castillo con 15 grupos y 14 entradas |
| Imagen superior | Modelo original de 1.370 triángulos, materiales/texturas y proyección de 400×240 |
| Estéreo | Dos vistas calculadas desde geometría, slider consultado; evidencia de emulador |
| Entrada | Circle Pad/C-stick en visor; adaptador de magnitud original compilado, pendiente de gameplay |
| Menú original | No se ejecuta todavía |
| Empezar partida y mover personaje | No implementado en el ejecutable |
| 60 FPS en Old física | Sin prueba física; el visor en emulador no demuestra este requisito |

## Evidencia conservada

- `build/package/verification.json`: identidad y hashes del CIA actual.
- `build/package/azahar-old/result.json` y `azahar-new/result.json`: hash exacto
  instalado, modelo emulado, log de arranque, conservación de ROM y salida.
- `build/native-heap/report.json`: 387 asignaciones ARM, alineaciones positivas
  y negativas, 12 rondas de liberación y recuperación de espacio, dos variantes
  de delete privado; cero llamadas a la ruta fatal.
- `build/native-heap/operator-isolation.txt`: separación de operadores del juego
  frente a asignaciones C++ de la plataforma.
- `build/native-core/report.json`, `counts.json` y `symbols.json`: compilación y
  dependencias reales. El enlace de la colección es relocatable, no ejecutable.
- Quince pruebas CTest de host pasaron tras los cambios del decoder, bajo
  AddressSanitizer y UndefinedBehaviorSanitizer. Se añadió la prueba persistente
  `game_projection` como decimosexta y pasó por separado. El cambio de resto
  firmado también pasó `fixed_math`, con 100.081 pares nuevos. Las pruebas
  adicionales de archivos y reubicación usan `build/assets.pak` real.

Los snapshots con geometría provienen de la ventana activa de Azahar. Una
captura negra de la ventana en segundo plano no se toma como prueba visual;
la ventana se activa y se comprueba la imagen antes de guardar la captura.

## Trabajo pendiente concreto

Prioridad tras la revisión del enfoque: integrar el arranque y el bucle original
del juego tomando como referencia la secuencia ya existente en el port de PC
fijado (`upstream/port/README.md`, `hal/title_entry.cpp`, `hal/level_boot.cpp` y
`ntr/rt.cpp`). La meta verificable es título → menú → selección de archivo →
partida controlable en el ejecutable 3DS. Conservar los adaptadores útiles ya
construidos; las nuevas pruebas aisladas deben resolver un bloqueo concreto de
esa integración. El port de PC usa mapeo de memoria DS y un planificador de
fibras con dependencias de plataforma, por lo que su compilación en 3DS no está
resuelta por reutilizar únicamente sus fuentes.

1. Resolver el bloqueo posterior a la petición real de Stage, con sus ciclos
   originales de actores y overlays, desde la RAM de tick 3859. La selección
   de archivo y la petición de nueva partida ya se observaron en el target.
2. Sustituir los accesos DS restantes con proveedores nativos de perfil,
   gráficos/GX, sincronización y sistema. Auditar el bloque alcanzado completo
   y comprobar sus contratos antes de un nuevo intento integrado.
3. Extender a la escena completa el aislamiento de asignación/liberación ya
   aplicado y verificado en el subconjunto de Player, y completar su heap.
4. Verificar Stage/Player y movimiento en el target con los recursos y estados
   originales. Verificar la cámara libre ya implementada en una partida real,
   incluidos los modos de escenas, pausa y entrega HID en modelos con C-stick.
5. Completar audio/guardado y medir en consolas físicas Old y New. Mantener la
   velocidad original de simulación al trabajar en salida de 60 FPS.

No se publicó una release ni se instaló software en una consola física.

Las secciones siguientes conservan evidencia histórica de subconjuntos. Su
ejecución aislada no sustituye el estado actual de la integración descrita arriba.

## Continuación: constructor, recursos y movimiento aislado

El constructor original de `Player` pasó una ejecución ARM11 en Unicorn con
datos relocalizados de la ROM EU aportada: 29 funciones visitadas, posición,
rotación, ID, matrices, pila y canario comprobados. Se resolvieron el conflicto
de símbolos C1/C2 de `dActor_c` y el error de compilación del inicializador
estático del jugador. Los 26 inicializadores del overlay 2 y `InitResources`
ya terminan en el diagnóstico: 98 solicitudes de archivos, 120 transferencias
de texturas y 62 de paletas comprobadas contra los buffers nativos. Player
aparece sobre el suelo original en `(-1200, 254, 6800)`. Esto aún no dibuja al
personaje. Una prueba independiente completa 180 ciclos: 60 de reposo, 60 con
derecha y 60 tras soltar. Yoshi avanza 443,8984375 unidades durante la entrada,
conserva el suelo y se detiene tras frenar. Visita 496 funciones y solicita 101
archivos, sin trampas ni accesos inválidos. El arranque normal de sesión se
detiene en la preparación del audio; no equivale al arranque desde el menú.

El diagnóstico ya utiliza 359.736 bytes reservados por el enlazador y un
cargador ARM11 que aplica 7.837 reubicaciones, con comparación independiente
de todos los bytes y rechazo sin cambios de una referencia corrupta final.
Los operadores `new/delete` del subconjunto de Player están aislados del
runtime de la plataforma. Ambas piezas reducen trabajo pendiente para la
integración, pero no incorporan Player al ejecutable de 3DS por sí solas.

El servicio nativo de archivos puede cargar los 198 miembros originales de
`arc0.narc`, incluidos modelos y animaciones del jugador. Todos los resultados
coinciden con ndspy en ASME0 y ASMP0; el servicio está integrado en el nuevo
ELF/3DSX, que todavía no tiene ejecución nueva en Azahar ni en consola física.
El CIA anterior no se ha reemplazado. Menú original, inicio de partida y
movimiento del personaje siguen pendientes.

Detalles, límites y comandos: [NATIVE_PLAYER.md](NATIVE_PLAYER.md).

## Continuación: renderizado original de Player

El diagnóstico ARM11 ejecuta ahora `Player::Render` durante los 180 ciclos
aislados: 521 funciones distintas, sin trampas, 4.965 palabras GX por ciclo.
Seis muestras decodifican 456 triángulos y tres texturas cada una. Esto amplía
la prueba de lógica anterior; no integra por sí solo al jugador en la aplicación.

Una aplicación gráfica separada reprodujo tres poses grabadas en Azahar Old.
Su registro confirma 3.600 dibujos, dos vistas y salida limpia de la aplicación;
el supervisor terminó después el proceso del emulador al alcanzar el límite.
No hubo comprobación visual de esa ventana, ejecución de gameplay ni prueba
física. Evidencia: `build/player-render-azahar/result.json`. El CIA de `dist/`
conserva su hash anterior. No se interpreta esta prueba como progreso del menú,
inicio de partida o rendimiento del juego.

## Antecedente: colisiones originales en ARM11

El objetivo sigue activo. El nuevo ELF/3DSX de `build/3ds/source/ctr/` incorpora
las consultas originales del terreno; **el CIA de `dist/` sigue siendo el paquete
anterior verificado en Azahar**. No se volvió a ejecutar Azahar para repetir el
visor y no se atribuyen al nuevo ejecutable sus capturas ni sus resultados.

Cambios verificables de esta continuación:

- Lectura del KCL y CLPS del castillo desde la ROM aportada: 1.243 triángulos,
  298 tablas de octree y 1.137 listas, con comprobación de todos los índices,
  offsets, profundidades y terminadores antes de reubicar punteros.
- Constructor, destructor, consulta de suelo y colisión de esfera originales,
  recompilados para ARM11 con las tablas virtuales que genera GCC. Se reemplaza
  exclusivamente el acceso a IRQ/raíz cuadrada de DS por raíz entera exacta.
- La consulta de línea procede de la adaptación del port de PC fijado; esa
  función está identificada por upstream como no byte-matched. Se conserva
  esa limitación en el manifest y el informe de pruebas.
- 567 llamadas ARM11 ejecutadas con Unicorn, sin mapear los registros de DS:
  289 consultas de suelo, 77 impactos verificados contra el plano del triángulo,
  77 coincidencias de rayos locales y 77 contactos de esfera; cuatro entradas
  originales, tres consultas negativas y cuatro inicializaciones inválidas.
  Se comprueban también el empuje de una esfera en el césped y 108 raíces
  enteras exactas; los casos geométricos atraviesan 23 llamadas a raíz cuadrada.
- Nueve pruebas CTest pasan con ASan/UBSan, incluida la validación de archivos
  corruptos. La prueba adicional con `build/assets.pak` verifica los datos reales.
- Compilación y enlace del ELF/3DSX de 3DS completados con el conjunto de
  colisiones. Su arranque comprobará las primeras cuatro entradas del castillo
  y registrará `original collision READY` sólo si las consultas son correctas.
  **Ese arranque nuevo todavía no se ha probado en emulador ni consola.**

Evidencia: `build/native-collision/report.json`, `objects.json`, `runtime.elf`
y `build.log`. CMake exige que el manifest coincida con el hash del que pasó la
prueba ARM y verifica los hashes de los objetos que incorpora. Los operadores
de asignación del juego siguen aislados de los de la plataforma.

En el diagnóstico anterior a la integración de Player, su frontera
conservadora de constructor/InitResources/Behavior tiene 228 referencias a
datos o tablas todavía sin reubicar, 51 servicios/aliases pendientes, 19
proveedores que requieren adaptación y 21 símbolos del runtime de compilador.
Esos últimos no son todos fallos del juego: el enlace final aporta el runtime.
También había un conflicto C1/C2 del constructor de `dActor_c`, ya resuelto
con propietarios explícitos. El informe histórico está en `build/native-player/`;
las pruebas actuales de Player y sus límites están en `NATIVE_PLAYER.md`.
Las colisiones aisladas no ejecutan Player, el menú ni una partida; no prueban
60 FPS ni velocidad de simulación correcta en ninguna consola.

## Verificación anterior conservada del paquete actual

CIA SHA-256: `a052d84a2c9739aeb57609cde308a78a22f0934b9930b95b61840f8cad4de607`.
Ambos perfiles aislados de Azahar, Old y New, instalaron y arrancaron este mismo
CIA; se observó el castillo estéreo y el texto de la pantalla inferior. Ambos
registraron `exit clean` mediante START y terminaron con código de proceso 0.
Los ocho tests de host con ASan/UBSan volvieron a pasar. No hubo prueba física.
La ROM europea se verificó para recuperar su catálogo, pero el ejecutable actual
mantiene el perfil de arranque USA ASME0.
