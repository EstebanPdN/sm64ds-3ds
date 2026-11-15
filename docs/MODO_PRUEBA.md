# Modo de prueba automático (SELECT+START)

> Registro fechado, en español. La guía vigente, en inglés, es
> [TEST_MODE.md](TEST_MODE.md).

Disponible desde BUILD 110 en la CIA de prueba nativa. Sigue el diseño del
modo de prueba de Worms 3D. Desde BUILD 114, SELECT+START recorre solo
**todos los niveles** del juego y escribe un informe por situación en cada
uno. La prueba de un solo punto de BUILD 113 sigue disponible con
R+SELECT+START.

## Novedades de BUILD 135 (comprobadas en Azahar; en consola, sin probar)

- Los volcados de un recorrido con Lock FPS 60 describen la imagen
  intermedia de un par (la que se dibuja como fotograma normal); la segunda
  imagen ya no se mezcla en sus contadores de dibujos y pasadas.
- Desarrollo (.3dsx, `env.txt`): `SM64DS_FPS_LOCK_CYCLE=N` pasa por 30 → 60 →
  Unlocked cada N actualizaciones, para probar el cambio de Lock FPS en plena
  partida. La CIA no lee `env.txt`.

## Novedades de BUILD 134 (comprobadas en Azahar; en consola, sin probar)

El recorrido usa el «Lock FPS» elegido en el menú de pausa antes de pulsar
SELECT+START (por defecto 30 en Old y 60 en New). Para medir el modo de 60 en
una Old: poner Lock FPS en 60, salir del menú y lanzar el recorrido.

Interruptores en la SD (archivo vacío; se leen una vez al arrancar):

| Archivo en `sdmc:/3ds/sm64ds/` | Efecto |
|---|---|
| `sin-recortes.txt` | Rutas de BUILD 133 (misma imagen, más lentas): sirve para el A/B de los recortes de BUILD 134. También desactiva la imagen exacta en paralelo. |
| `audio-espera.txt` | El juego vuelve a esperar al mezclador de audio (BUILD 133). |

Campos nuevos de cada `resumen.txt`:

- Cabecera: `limite 30 FPS`, `limite 60 FPS` o `limite sin FPS` (Unlocked).
- `== interpolacion ==` (desde el informe anterior):
  - `intermedias`: imágenes interpoladas presentadas;
  - `exactas_tras_intermedia` y `exactas_en_paralelo_con_gpu`: imágenes exactas
    del par, y cuántas se construyeron mientras la GPU dibujaba la intermedia
    (solo Old);
  - `presupuesto_cabe` / `presupuesto_no_cabe`: actualizaciones en las que el
    par cabía o no en el 92 % del periodo (si no cabe, una imagen);
  - `segunda_imagen_perdida`: una actualización encontró el render ocupado
    con un par (después hay `espera_updates` actualizaciones a una imagen);
  - `costo_imagen_ms`, `costo_segunda_ms`: CPU medida de cada imagen del par.
- Audio: `juego_espera_tras_imagen_ms` y `lotes_aplazados` (lotes de comandos
  entregados después de pasar la imagen al render). Con `audio-espera.txt`
  vuelven a contarse en `juego_espera_ms`.
- Productores por etapa: ahora «ms por actualización con captura».
- La «subida» del hilo de render incluye ahora la subida de skin (antes no
  se medía), así que sube respecto a BUILD 133 sin que haya más trabajo.

Comparar dos volcados de la misma imagen (por ejemplo con y sin
`sin-recortes.txt`): `python3 tools/native_renderer/pipeline/compare_gpu_state.py VOLCADO_A VOLCADO_B`
dice las palabras de comandos de cada uno y si el estado de la GPU en cada
dibujo es el mismo.

## BUILD 133: corrección del bloqueo durante el primer volcado

Los registros físicos de Old 3DS en `dumps/prueba131` muestran dos paradas:
la primera en el barrido de registros internos PICA (los tres bloques externos
se escribieron completos); la siguiente, con ese barrido ya omitido, durante
`vram.bin` (4.325.376 de 6.291.456 bytes). No alcanzaron la memoria DS ni RAM.

El recorrido ya no lee registros GPU ni VRAM física. Guarda
`lecturas_omitidas.txt` para dejar constancia; conserva comandos PICA, dibujos,
escena, texturas, capturas y memoria de vídeo DS. La RAM conserva su frecuencia
anterior, pero excluye los dispositivos, para no volver a leer VRAM desde
`memoria.vm`. El volcado manual mantiene su comportamiento anterior.
Cada archivo deja una línea en el log antes de escribirse.

No hace falta borrar `registros-pica.probando`: BUILD 133 no lo usa.
La confirmación de esta corrección en hardware está pendiente.
Las referencias a volcados físicos siguientes describen BUILD 131.

## Novedades de BUILD 131 (comprobadas en Azahar; en consola, sin probar)

- **Más allá de la entrada.** Después del guion de la entrada, el recorrido
  visita hasta **cuatro zonas** más de cada nivel y mide tres situaciones en
  cada una (quieto, vuelta de cámara y correr en círculo). Las zonas se
  eligen solas: actores vivos del nivel que están sobre suelo, a más de 1200
  unidades de la entrada y entre sí; el personaje aparece a 220 unidades del
  actor, del lado de la entrada. Si muere en una zona, se abandona esa zona
  (no el nivel) y el recorrido sigue con la siguiente.
- **Más situaciones en la entrada:** 24 en lugar de 20 (agachado, gatear,
  barrido y patada en el aire).
- **Volcados.** Tras el primer informe de cada nivel (`volcado/`) y de cada
  zona (`volcado-zonaN/`) se guardan los comandos que recibió la GPU en una
  imagen, un registro por dibujo, la escena, las texturas, los registros de
  la GPU y todos los actores. Una vez por nivel, además, la VRAM y la memoria
  de vídeo de DS. La RAM completa del proceso (unos 80 MB) se guarda en el
  primer nivel; para tenerla en todos, crea el archivo vacío
  `sdmc:/3ds/sm64ds/prueba/ram.txt` antes de empezar.
- **Modelos sin dibujar.** Cada informe lista qué modelos no dibujó el render
  nativo y por qué (antes solo había un contador).
- **Informes sin carpeta propia.** Cada informe son cuatro archivos sueltos
  dentro de la carpeta del nivel. Crear una carpeta por situación costaba
  unos 2,4 s de los 3,3 s que tardaba cada informe en la tarjeta de la
  consola.

- **Lo que encontró el primer nivel con zonas.** El personaje recibe daño
  en las zonas, y eso sacó a la luz dos fallos que la entrada nunca enseñaba:
  el medidor de vida (y todo el marcador de la pantalla superior) salía
  **boca abajo, en la parte baja y dejando rastro** en Old 3DS desde BUILD
  114, y **ningún personaje tenía sombra**. Los dos están corregidos en
  BUILD 131.

El recorrido completo son ahora hasta 36 informes por nivel. Su duración en
consola está sin medir.

**Cómo leer las zonas.** En los niveles hechos de salas (las plantas del
castillo, el sótano) el personaje puede aparecer en una sala que el juego
todavía no ha mostrado, porque no ha pasado por su puerta: la imagen sale
con el personaje sobre negro. Es un efecto de la prueba, no un fallo del
juego. Tras llegar a una zona la prueba espera 3 s a que llegue la cámara.

## Recorrido de todos los niveles (SELECT+START)

1. Con el juego abierto (título, menú o dentro de un nivel), pulsa
   **SELECT+START a la vez**. Arriba aparece «REINICIANDO» y el juego se
   cierra; la consola lo vuelve a abrir sola (HOME Menu, cargador
   encadenado). En Azahar hay que abrirlo a mano.
2. Cada nivel se juega desde un arranque limpio: título →
   `StartFile` original → nivel. Los 50 niveles van del 1 al 51 (el 0 es un
   mapa de pruebas y el 41 no tiene curso), en el orden de las tablas de la
   propia ROM: castillo, los 15 cursos con sus subzonas, cursos secretos,
   Bowser con sus arenas, jefes de los personajes y la sala de juegos.
3. En cada nivel el personaje rota (Mario, Luigi, Wario, Yoshi) para que
   todos los cuerpos aparezcan en todo tipo de mapa. Si un nivel no permite
   el cambio, se juega con el que tenga y se anota «sin cambiar».
4. Guion de 24 situaciones (20 hasta BUILD 130) desde la entrada del nivel: quieto, cámara a
   izquierda y derecha, andar, correr, curva, correr girando la cámara,
   salto, triple salto, golpes, golpe corriendo, salto largo, voltereta,
   **menú de pausa**, golpe al suelo, giro en el sitio, cámara con L,
   cámara con C-Stick, correr hacia la cámara, quieto final y, desde BUILD
   131, agachado, gatear, barrido y patada en el aire. Entre acciones vuelve
   a la entrada. Después vienen las zonas (ver arriba).
5. No toques nada: la franja superior indica «NIVEL 07 05/36» (nivel e
   informes hechos). **START cancela** el recorrido. Al terminar, la pantalla
   inferior muestra «RECORRIDO FINALIZADO», niveles completos, incompletos y
   cuelgues, FPS medio y la carpeta; START reinicia el juego normal.

Duración aproximada en consola: 1,5–2 minutos por nivel (unos 90 minutos en
total). Conviene tener la consola cargando. Desde BUILD 122 cada arranque
verifica el paquete `.sm64ds` con lecturas grandes: unos 3–6 s en lugar de
los 28 s de BUILD 120.

Casos conocidos (Azahar): el nivel 31 (torre del ala) devuelve al jugador a
otro nivel nada más entrar, así que sale INCOMPLETO; el patio del castillo (3)
mantiene un bloqueo de pausa del jugador y el guion empieza tras 10 s de
espera («sin pausa» en el resultado). Las cifras «modelos_faltantes» de los
informes son dibujos de modelos que aún no tienen banco nativo.

Si el personaje muere y reaparece en el mismo nivel, la prueba espera y
repite la acción en curso. Si sale del nivel (lava, caída al vacío, derrota
de jefe) después de empezar su guion, desde BUILD 124 el juego **se reinicia
y vuelve a entrar en ese nivel**, salta la situación que lo mató y sigue con
la siguiente, conservando los informes y medias ya tomados (línea
`-- ... reentrada N de 3` en `resultado.txt`, «reentradas N» en la línea del
nivel). Como máximo 3 reentradas por nivel; después, o si el nivel se sale
antes de empezar el guion (la torre del ala, 31), se anota INCOMPLETO y se
sigue. En BUILD 123 se perdieron así 20 de 50 niveles (lava 5/20, jefes de
Bowser 4–11/20).

**Partida propia (BUILD 124).** La prueba juega sobre su propio archivo,
`prueba/partida.sav`; tu partida (`native-test.sav`) no se lee ni se escribe.
El recorrido empieza cada nivel con una partida nueva, igual que cada sesión
de Azahar; la prueba de un punto usa una copia de tu partida hecha al pulsar
el acorde. En BUILD 123 el recorrido usaba tu partida: con ese progreso, el
patio del castillo abría el menú «Save & Continue» de fin de curso y 14 de sus
20 situaciones midieron ese menú (15 FPS), y las pulsaciones de A del guion
podían guardar encima. Ese menú lo abre la propia entrada del patio (también
en Azahar con partida nueva) y mantiene el bloqueo de pausa: desde BUILD 124
el guion pulsa START cada 2 s mientras dura el bloqueo con control, el menú
se cierra (resultado: «menu cerrado con START») y el nivel se mide jugando.
Con la pausa bloqueada START no puede abrir otra pausa. La etiqueta puede salir también
en otros niveles cuyo bloqueo de entrada (el cartel del título del curso)
termina justo después del toque; es inofensivo.

**Fallos.** Desde BUILD 117, un error fatal del juego (memoria agotada,
`abort`) ya no termina en la pantalla de error de Luma: el juego escribe el
motivo en el log (líneas `FATAL`), para el sonido y el render, y se cierra
limpio. Durante el recorrido **se vuelve a abrir solo** y sigue con el nivel
siguiente; ese nivel se anota «CUELGUE O CIERRE» y el log del arranque que
falló queda en `LL-nivel/registro_cuelgue.txt`. Si aun así la consola se
queda colgada o sale la pantalla de Luma (fallo de otro tipo), apágala y
vuelve a abrir el juego: continúa igual, y Luma deja un volcado en
`luma/dumps/arm11/`. El log del arranque anterior se conserva siempre como
`native-test-anterior.log`.

**Sin hilos (BUILD 122).** En BUILD 120 el recorrido se paró tras 25
arranques seguidos: el 25 no pudo crear el hilo de render y el 26 ni el del
juego, con memoria de sobra. Todos los arranques intentaban además un hilo
de precarga en el núcleo 1, que en consola siempre fallaba; ya no se crea.
Cada arranque y cada informe anotan los límites del sistema (hilos en uso y
máximo, sección «sistema» del resumen). Si aun así un arranque no puede
crear su hilo de render o de juego, el nivel se anota «NO ARRANCO», no se
cuenta como cuelgue, el juego vuelve al menú HOME y el recorrido **repite ese
nivel** en el siguiente arranque: reinicia la consola y abre el juego.

**Consola cerrada (BUILD 128).** Desde que pulsas SELECT+START la consola
no se suspende: puedes cerrar la tapa y la prueba sigue. Con la tapa
cerrada las pantallas se apagan (se encienden al abrirla). Al terminar, el
**LED de notificación se pone verde fijo** y queda el panel final; al pulsar
START en él, el LED se apaga y el juego vuelve a la normalidad (con
suspensión). Si el recorrido no puede continuar (no se pudo escribir el
estado o un nivel «NO ARRANCO»), el LED queda **rojo**. Cancelar con START
no enciende el LED. Cada arranque de nivel rechaza la suspensión desde su
primer instante (`__appInit`), porque libctru pide dormir si la tapa está
cerrada justo al iniciar la aplicación, y la preparación de datos no
espera a que se abra la tapa durante una prueba. El log anota
`PRUEBA power: sleep refused=1 ...` y `PRUEBA notification LED green
result=00000000`. Sin una prueba en curso el juego se suspende como siempre.

**Al terminar (BUILD 130).** Cuando el recorrido acaba, el juego queda en
reposo detrás del panel final: sin sonido y casi sin trabajo (una
actualización cada 50 ms, cada 250 ms con la tapa cerrada), hasta que
pulses START. BUILD 128 seguía jugando a plena carga durante una hora
después del LED verde. La tapa ya no se consulta desde el hilo del juego:
un hilo de baja prioridad la mira dos veces por segundo. En BUILD 128 esa
consulta provocaba casi todos los tirones de 100-314 ms. El primer toque de
START ante un bloqueo de pausa espera 3 s, así que las animaciones de
entrada no se tocan. Una muerte detectada mientras el personaje vuelve al
punto de inicio ya no se salta el paso siguiente: la reentrada lo repite.

## Prueba de un solo punto (R+SELECT+START)

Dentro de un nivel, mantén R y pulsa SELECT+START: igual que BUILD 113, el
juego se reinicia, vuelve a ese nivel, personaje y punto exacto, y juega el
mismo guion de 20 situaciones allí.

## Qué se escribe en la SD

`sdmc:/3ds/sm64ds/prueba/NN/` (01–99):

| Archivo | Contenido |
|---|---|
| `resultado.txt` | Cabecera (BUILD, Old/New), una línea por informe (FPS, p95, máximo, juego, render, GPU, dibujos, actualizaciones sin imagen) y una línea `== NN/50 nivel ...` por nivel (resultado, carga, FPS medio, peor p95). Al final, la línea `RECORRIDO ...` con los totales. |
| `LL-nivel/SS-situacion.resumen.txt` (hasta BUILD 130: `SS-situacion/resumen.txt`) | FPS e intervalos p50/p95/p99/máx; hilo del juego (actualización, preparación 2D, productores 3D, skin); hilo de render (trabajo, composición 2D, dibujo, espera GPU, subida, envío, GPU anterior); dibujos, triángulos, matrices, filas de luz, KB subidos; memoria libre; actualizaciones sin imagen (secas por retraso o por tope de 30, saltadas tarde, descartadas con el render ocupado); salud del render (modelos faltantes, fallos de productor, de partículas, imágenes rechazadas). Desde BUILD 117 la memoria incluye el montón libc (`heap_libre`, usado, bloques libres y sin asignar). Desde BUILD 122: `render_cpu_estimado` (trabajo del render menos las actualizaciones del juego que le quitaron el núcleo 0), `etapas_dibujo` (validar, espera, recursos, dinámico, previo, plan, geometría, composición, final), `ruta_2d` (imágenes con 2D por CPU arriba, preparadas de nuevo, mapa por GPU), `secas_por_render_ocupado` y la sección «sistema» (hilos, eventos, mutex... en uso/máximo). Desde BUILD 124, «sistema» dice dónde corren el render y el audio (`nucleo_render`, `nucleo_audio`, `render_comparte_nucleo`, `cpu_nucleo1` = % del núcleo 1 concedido); con el render en su propio núcleo, `render_cpu_estimado` es el trabajo sin restar nada. |
| `LL-nivel/SS-situacion.cuadros.csv` | Una fila por imagen presentada (2 s de contexto + la ventana medida), en ticks de 268.111.856 Hz. BUILD 122 añade `game_overlap_ticks`, `stage_*` y `route_2d` (bits: 1 menú nativo, 2 2D por CPU arriba, 4 A preparada, 8 B preparada, 16 mapa por GPU, 32 cielo). |
| `LL-nivel/SS-situacion.arriba.bmp`, `.abajo.bmp` | Las dos pantallas reales en el momento del informe. |
| `LL-nivel/SS-situacion.faltan.csv` | Solo si algún modelo no se dibujó (BUILD 131): una fila por modelo con llamadas, motivo (1 su carga no se vio, 2 sin banco nativo, 3 identidad doble), fichero, huesos, materiales, texturas y los nombres de su primer material y textura. |
| `LL-nivel/volcado/`, `LL-nivel/volcado-zonaN/` | Volcado del primer informe del nivel y de cada zona (BUILD 131), ver abajo. |
| `LL-nivel/registro_cuelgue.txt` | Solo si ese nivel no terminó: el log completo del arranque que falló (BUILD 117+). |

Desde BUILD 131 el resumen de cada informe añade tres secciones: «estado
del juego» (posición, velocidades, salud, agua, suelo, banderas de estado y
actores vivos), «productores por etapa» (materiales, poses, bases de
normales, bases de luz, partes y skin, en ms por actualización, con las
normalizaciones de vectores) y «modelos sin dibujar».

### Volcados (BUILD 131)

| Archivo | Contenido |
|---|---|
| `comandos_gpu.bin` | La lista de comandos PICA200 de una imagen completa (todas sus pasadas) y un registro por dibujo. |
| `dibujos.csv` | Los mismos dibujos en texto: pasada, malla, textura, tamaño, formato, índices, iluminado, lote, mezcla, profundidad. |
| `escena.json` | La escena que dibujó el render: ocurrencias, partes, materiales y quads. |
| `texturas.csv` | Las texturas y destinos de render del render nativo (dirección, tamaño, formato, VRAM). |
| `gpu_estado.txt` | Memoria de GPU libre y usada, recursos, contadores acumulados y el cuadro capturado. |
| `gpu_registros.bin` | Registros leídos por GSP: controladores de pantalla, motor de transferencia y el bloque interno de la PICA (0x401000-0x401BFF). Si esa lectura no terminara nunca en una consola, queda el archivo `prueba/registros-pica.probando` y los arranques siguientes ya no la intentan. |
| `actores.csv` | Todos los actores vivos: identificador, área, posición y distancia al jugador. |
| `vram.bin` | Los 6 MiB de VRAM (solo en `volcado/`). |
| `ds_video.bin` | Registros de E/S, paletas, OAM y VRAM del lado DS que aún usa el 2D. En `volcado/` está toda; en las zonas, solo la parte de los sprites. |
| `planos_2d.bin` | Los tres planos de 256×192 que compuso el 2D por CPU: detrás del 3D, delante del 3D y la otra pantalla. Comparados con `vram.bin` dicen si un fallo del 2D nace en la CPU o en la GPU. |
| `memoria.vm` | Toda la memoria legible del proceso, formato SM64VM1 (primer nivel, prueba de un punto, o todos los niveles con `prueba/ram.txt`). Se lee con `tools/inspect_integrated_vm.py` y el ELF de ese BUILD. |

Tamaño aproximado: unos 13 MB por nivel de volcados (más la RAM donde se
pida) y 0,6 MB por informe.

En la prueba de un punto las situaciones están en `informes/` como antes.
El estado entre arranques es `prueba/estado.txt` (se borra al terminar).
Cada informe ocupa unos 0,6 MB: el recorrido completo, unos 600 MB.
El tiempo de escritura de cada informe se excluye del reloj del juego y de
las medidas. Desde BUILD 122 todo lo que escribe un informe (carpetas
incluidas: en la tarjeta del propietario crear una tardaba ~2,4 s) ocurre
con el render parado dentro de esa pausa; en BUILD 120 la imagen en curso
absorbía ese tiempo y aparecían tirones falsos de 1,4–2,5 s al final de las
situaciones. `sin_imagen` cuenta ahora todas las actualizaciones sin imagen
(antes no sumaba las descartadas con el render ocupado). Los temporizadores finos están activos durante la prueba
(cuestan aproximadamente 1 ms por imagen); el FPS normal es algo mayor.

## Devolver los resultados

Copia la carpeta `3ds/sm64ds/prueba/` de la SD al Mac y ejecuta:

```sh
python3 tools/test_mode_report.py /ruta/a/prueba --markdown informe.md
python3 tools/test_mode_report.py /ruta/a/prueba --detalle   # una fila por situación
```

Para los volcados:

```sh
python3 tools/test_mode_dump.py /ruta/a/prueba            # resumen de cada volcado
python3 tools/test_mode_dump.py /ruta/a/prueba/01/06-bob-omb/volcado --dibujos --comandos lista.txt
python3 tools/test_mode_dump.py /ruta/a/prueba/01/06-bob-omb/volcado-zona2 --sprites --planos planos/
python3 tools/test_mode_sheet.py /ruta/a/prueba hojas/     # una hoja de capturas por nivel
```

`--sprites` lista cada sprite de la OAM que está en pantalla y `--planos`
escribe los planos 2D como PNG. `test_mode_sheet.py` junta todas las capturas
de un nivel en una sola imagen (`--abajo` para la pantalla inferior): es la
forma rápida de repasar los 50 niveles a ojo.

Da, por volcado, cuántas palabras de comandos y de uniformes se enviaron por
dibujo, las escrituras por grupo de registros, los dibujos pequeños que se
podrían agrupar, el estado de las pantallas y del 2D de DS y los actores.
No mide velocidad: dice qué trabajo de CPU queda por quitar.

Genera una tabla por nivel, el resultado de cada nivel, los niveles y
situaciones más lentos, la memoria mínima libre (lineal y montón libc), las
líneas `FATAL` de los niveles que no terminaron y la lista de problemas del
render. Las fases se anidan y no se suman entre sí.

## Implementación

- BUILD 131: zonas (`choose_zones` en `test_mode.cpp`, sondas del juego en
  `test_mode_game.cpp`: lista de actores y `dBgCh_Gnd`), volcados
  (`sm64ds_fresh_native_test_dump` en `game_runtime.cpp`, captura de
  comandos en `citro3d_backend.cpp`) y censo de modelos
  (`model_components_hooks.cpp`). Desarrollo: `SM64DS_PRUEBA_ZONAS=N` en
  `env.txt` limita las zonas (0 las quita).
- `source/integrated_native/renderer/test_mode_script.hpp`: guion, tabla de
  niveles, mando sintético, formato del estado (V2; lee también el V1 de
  BUILD 113) y máquina de estados (probada en host:
  `tests/native_renderer/test_mode_script.cpp`).
- `test_mode.cpp` (libctru) y `test_mode_game.cpp` (cabeceras de la
  decompilación): estado en SD, acorde, reinicio por nivel, registro de
  cuelgues, `StartFile` / `SetRealCharacter` originales, colocación del
  personaje y estado en pantalla.
- `game_runtime.cpp`: wraps de HID (teclas, Circle Pad, C-Stick, táctil),
  informe `sm64ds_fresh_native_test_report` y franja/panel en pantalla.
- La partida del propietario nunca se toca: `SM64DS_SAVE_PATH` apunta a
  `prueba/partida.sav` durante la prueba (borrada en cada nivel del
  recorrido). En memoria solo `SaveData::SetCharacterIntro(7)`, como el
  benchmark verificado.
- Desarrollo en Azahar: `SM64DS_PRUEBA_PASOS=N` en `env.txt` (solo el
  .3dsx; la CIA no lee `env.txt`) corta cada nivel tras N situaciones, y
  `tools/native_renderer/pipeline/verify114.py` encadena los arranques.
