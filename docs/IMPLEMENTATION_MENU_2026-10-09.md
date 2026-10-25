# Continuación del menú original — 9 de octubre de 2026

Old 3DS sigue siendo la plataforma base. El objetivo de menú, nueva partida,
movimiento y 60 FPS físicos permanece pendiente. Se continuó el target general
existente; el CIA del visor y la ROM original se conservaron.

## Cambios integrados y controles negativos

- Se retiraron 25 reubicaciones falsas exactas: una dentro del LZ10 del logo de
  Boot y 24 dentro de NCSC/NCCG del menú. Las tablas originales habían tratado
  palabras de gráficos como direcciones. El ELF anterior reproduce daños de
  4.756 bytes en cada uno de dos bancos del logo. El test nuevo descomprime los
  streams mediante las instrucciones ARM11 reales y coincide con `ndspy`.
- La auditoría de OV007 cubre 21 archivos Nitro completos, 120 bloques y
  174.136 bytes. No quedan otras reubicaciones dentro de esos archivos; se
  conservan las referencias reales desde las tablas del menú.
- Se recuperó el argumento de modo de `b6eb4 → bd648`. La declaración
  recuperada había perdido el argumento que la ROM carga desde `+0x1c`.
- Se publica el inicializador de globals compartidos tras el catálogo EU. El
  ELF anterior retorna éxito del cargador pero deja seis globals a cero y
  reproduce un salto a PC cero; el ELF nuevo pasa el mismo control y valida
  tabla trigonométrica, escala y ocho callbacks/defaults reales.
- Se adaptaron selección/copia de bancos, paletas, OAM, blending, cuatro hojas
  matemáticas y snapshots del cociente nativo. Cuatro constructores de matriz
  puros nuevos, junto con dos ya presentes, pasan 7.024 comparaciones ROM/ARM11.
- El compositor ordena BG activas una vez, intercala OBJ con los desempates
  originales y evita trabajo cuando OBJ está apagado. El tap de engine A
  conserva la composición antes del brillo maestro y de la selección de salida.
- Se conectó un target GPU dedicado 256×192 al backend de captura BGR555,
  con banco/offset/serial real. Una fixture encontró y corrigió la omisión de
  overlay con 3D desactivado. Dos fixtures finales, con y sin geometría,
  coinciden en los 196.608 bytes completos de cada imagen.
- Se corrigió la orientación/crop de las texturas de pantalla y se retiró la
  consola de texto libctru que interfería con la pantalla inferior Citro3D.
- El registro personal original se obtiene mediante CFGU, con pruebas del
  exportador y del llamador ROM. La inicialización real por CFGU completó en
  el último intento integrado, sin registrar nombre ni cumpleaños.
- `G3i::LookAt_` conserva las matemáticas, el ABI y el stream de cámara. Sus
  850 comparaciones ROM/ARM11 y 20 streams completos pasan; esto no equivale
  a controlar la cámara durante una partida.
- Los constructores del menú pasan 48 comparaciones de objeto, pila, handles y
  punteros contra la ROM y un control negativo que detecta un argumento dañado.

Los cambios conservan los fuentes upstream fijados. El generador comprueba
hashes, cantidades de sitios MMIO y reconstrucción inversa de cada modificación.
Ninguna comparación de fixture se presenta como ejecución del juego.

## Intentos integrados acotados

| Intento | Resultado observado |
|---|---|
| `1791591978270823000` | Logo corrupto; baseline conservado para reproducir la reubicación errónea |
| `1791592448731051000` | Boot 120 ciclos; detención por E/S de menú aún sin adaptar |
| `1791593448151098000` | Logo/copyright legibles y orientados; salto nulo de callback al solicitar escena 1 |
| `1791593798965199000` | Callback superado; STOP `func_02052ec8` en matrices del menú |
| `1791594468176316000` | Matrices superadas; lectura `027ffce4` en `func_02059eb0` y emisión GX en `G3i::LookAt_` |
| `1791595263336119000` | Perfil CFGU/cámara superados; MMIO Toon/matriz/materiales y rechazo GX al renderizar escena 1 |
| `1791596592223444000` | Bloque gráfico superado sin acceso de memoria inválido; rechazo TEXGEN2 auténtico de la estrella del título |
| `1791610976045409000` | TEXGEN2/proyección/LenVec3 enlazados; primer stream de escena 1 presentado (78 triángulos); lectura nula `0x84` en lookup SEQARC directo |
| `1791611331354038000` | Prueba temporal afectada por una instancia residual de Azahar y su diálogo de inicio; sólo Boot parcial, sin conclusión de título |
| `1791611477172386000` | 360 ciclos originales, sin fallo de memoria/CPU ni STOP; estrella animada y «Touch to Start» visibles, pantalla superior negra; interacción sin verificar |

El último ejecutable observado en esta etapa del 9 de octubre tiene SHA256
`6252b66cc3018b7fc7adcd43070824e1171664f2db2d93b39d901d02693a5888`, con ELF
`62cbc16c07a173da5b6655a33fbc79fab23cee7171e1059458e83d10521ef044`. Cada
directorio inmutable conserva ELF, 3DSX, logs y dumps. El supervisor rechaza
repetir el último binario idéntico y corta el primer fallo. La observación UI
del título parcial pertenece exclusivamente al intento `1791611477172386000`
y está descrita en su `visual-observation.json`. El título completo, el menú
interactivo y la partida siguen pendientes. No se atribuye a estos intentos
evidencia de consola física.

La continuación del 10 de octubre conserva una adquisición más reciente,
`1791616229939712000`, con toque HID normal, título listo y tres dumps completos.
Su ELF es `22a49e1552a15a6972cc0db302222ebd91ce58548cd0f0ec286beaff0708dde0`.
Las reproducciones de esa RAM ya pasan Adventure y selección de una ranura
vacía, y ejecutan la salida original del menú hasta el constructor de Stage.
También localizan la inversión V de las texturas y el ajuste de tabla virtual
que necesita ese constructor manual. La reparación de Stage y sus cargas
gráficas se compila y verifica antes del siguiente intento integrado. Estado
vigente y límites: [STATUS.md](STATUS.md) y
[NATIVE_RAM_DIAGNOSTICS.md](NATIVE_RAM_DIAGNOSTICS.md).

## Bloque de audio desactivado y vectores cortos

LenVec3 se recompiló en el target general y se comprobó su proveedor único.
Al superar el primer dibujo, la ruta OV007 alcanzó una llamada SDK SEQARC que
evadía la frontera Sound::Play y desreferenciaba una raíz nula. Las tres
entradas relacionadas (`020518dc`, `02051f1c`, `02051e60`) ahora devuelven
fallo SDK dentro de la política existente de audio desactivado. No crean una
raíz ficticia ni handles de reproducción. El reproducer mantiene ocho
controles negativos originales, seis registros SEQARC vacíos reales, 33
casos de los callers OV007/Player y tres callers de seis argumentos.

La misma rama alcanza `02053644` y `02053380`, longitud y normalización de
vector corto. Los proveedores nativos conservan raíz entera, wrapping y
lecturas secuenciales con destinos parcialmente solapados. Pasan 2.335
comparaciones de longitud, 14.010 de normalización y 146 del caller con alias.
Los canarios, extensión exacta de lectura/escritura y ABI se comprueban sin
mapear MMIO DS. El baseline reproduce los registros DS de IRQ/raíz/división.

La auditoría posterior conserva los ocho proveedores únicos y el archive
original sin definiciones duplicadas. Reutiliza los 4.912 vectores y 307
callers de LenVec3 tras comprobar que su fuente y sus instrucciones siguen
equivalentes; no repite la auditoría por un cambio diagnóstico del frame.
Reportes: `build/native-short-vector/report.json`,
`build/native-runtime/audio-deferred-test.json` y
`build/native-integration-check/final-provider-check.json`.

## Diagnóstico actual del título

La escena original 1 es `dScDSMT` (OV007); `dScTitle` (OV003) corresponde al
selector de depuración. El primer snapshot, tick 124, contiene todos los
0x8000 bytes de tiles del logo idénticos a la ROM y la paleta cargada. Las
capas BG2/BG3 todavía están apagadas y el fade es negro en ese punto; sus
mapas aún vacíos no explican por sí solos la pantalla observada más tarde.
El replay del compositor host coincide con sus tres capas capturadas. Un
escenario host separado con los mapas originales, capas activadas y fade
cero coincide con un decoder independiente: es una prueba de la ruta de
composición, no un frame producido por la partida.

«Touch to Start» se inicia con un toque nuevo o B nuevo después de las fases
de introducción y de opacidad; A usa otra rama de navegación. La prueba
aislada de ese gate coincide en ROM y ELF. Los checkpoints posteriores y el
registro de entrada consumida permitirán comprobar el estado real antes de
atribuir el bloqueo a entrada o gráficos. Audio, conexión de C-stick,
persistencia integrada de guardado y rendimiento físico siguen pendientes.

## Medidas y límites

El compositor de host, 40 muestras, obtuvo medianas 1,676958 → 0,695542 ms.
Boot en Azahar, siete muestras por binario, obtuvo medianas emuladas de scanout
211,751 → 105,748 ms. Son comparaciones diagnósticas separadas. Ninguna fija
FPS, velocidad del juego o consumo de recursos en una consola física.

Los reportes están en `build/native-runtime/`, `build/native-menu-math/`,
`build/native-identities/`, `build/native-capture/`, `build/menu-data-audit/` y
`build/native-capture-gpu-azahar/`. La captura actual no admite Source B,
mezcla A+B, 3D sola ni blank forzado. El compositor conserva límites previos
de blending 3D, efectos por scanline y ciertas rutas de paleta/OBJ.

El bloque gráfico siguiente adapta diez fuentes completas: iteración de
partículas, binding de textura, seis callbacks y dos helpers de quads. El
generador comprueba 60 fuentes y 496 sitios MMIO. Dos constantes de VTX_10 que
parecen direcciones se permiten únicamente en sus asignaciones RHS exactas;
un cambio de valor o nueva desreferencia falla en controles negativos.
Los proveedores SDK conservan el upload Toon de 32 halfwords y el comando
FIFO `0x19` con nueve valores de rotación y tres de traslación. Pasan 580
comparaciones ROM/ARM11; el host comprueba 104 streams completos, 144
triángulos transformados, canarios y overflow bajo ASan/UBSan. El bit de luces
de POLYGON_ATTR también se corrigió; la regresión de 12 colores falla cuando
se retira únicamente esa asignación y pasa con ella.

Se conserva el primer stream antes de decodificar junto a buffers de textura,
paleta y estado de sus bancos. Esto permite repetir el decode en host desde
datos del intento, sin abrir de nuevo el juego. El rechazo distingue textura,
comando y paquetes incompletos. El nuevo intento no presenta accesos MMIO
inválidos y conserva 1.466 entradas del primer frame original de la escena 1,
aún rechazado antes de presentarse. El rechazo es TEXGEN2, solicitado por
`mat_body` del BMD original `face_demo_mariostar.bmd`, handle 649. Las
consultas reales de textura comprimida y A5I3 resuelven y no quedan paquetes
incompletos. La implementación de TEXGEN2 ya pasa 442 casos y 1.134 UV de
fixtures con sanitizadores. El stream real ahora acepta 78 triángulos y sus
180 UV TEXGEN2 coinciden exactamente con la expresión entera del manual NITRO.
Retirar únicamente el guard no pasa estas pruebas. La matriz de textura tiene
su propio STORE/RESTORE 0, conservando el slot de posición.

La auditoría de proyección encontró seis triángulos generados con una matriz
distinta de la final. El renderer ahora copia y aplica la proyección de cada
batch para pantalla y captura; los 78 campos coinciden con los observadores
de los tres vértices. Pasan 9.284 comprobaciones y 2.131 comparaciones de clip
de host. El control anterior falla en los seis triángulos afectados.

La revisión del llamador de entrada identificó `cstd::mod` tras mantener una
dirección 20 ciclos. Se añadió el proveedor de resto firmado al archive nativo
existente. Pasan 593 pares ROM/ARM11 y 24 casos del caller real, incluidos los
ciclos 1/19/20/21/29/30 en las cuatro direcciones. Cuatro controles del baseline
se detienen exactamente en el registro DS de división, que queda sin mapear en
la prueba del proveedor nativo. El host verifica 100.081 pares con una
referencia de enteros anchos y sanitizadores. No hubo otra apertura para estos
análisis ni verificación visual del título/menú.
