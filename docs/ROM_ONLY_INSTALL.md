# Instalación automática desde la ROM

El candidato integrado nuevo prepara los datos del juego en la consola. La
única entrada que aporta el usuario es su ROM europea original, con cualquier
nombre terminado en `.nds`, dentro de esta carpeta:

```text
SD:/3ds/sm64ds/
  Super Mario 64 DS (Europa).nds
```

Se admite Europe, ASMP, revisión 0, 16.777.216 bytes y SHA-1
`1367529f2cb23e76ef295cb1727333ae8f0a6cd7`. Una ROM de otra región o una ROM
modificada se rechaza antes de crear el contenedor. La aplicación conserva la ROM,
las partidas y los ajustes que ya existan.

No hace falta renombrarla a `rom.nds`. La extensión se reconoce sin distinguir
mayúsculas (`.nds`, `.NDS`, `.NdS`), y los nombres pueden contener espacios o
caracteres Unicode admitidos por la SD. Se busca únicamente en esa carpeta.
Si hay varios archivos, se prueban los candidatos compatibles por nombre;
una copia que no supera el SHA-1 y CRC completos se descarta antes de escribir
el contenedor y se continúa con la siguiente. Cambiar el nombre de una ROM válida
no invalida el contenedor ni mueve las partidas.

Instala el CIA del candidato **ROM Only** mediante FBI y abre SM64DS Native.
La pantalla de preparación usa las imágenes y la tipografía auténticas del
menú de SM64DS, incluidas en el ejecutable: cielo, castillo, patrón azul y logo.
Los detalles de la barra se generan píxel a píxel con la paleta del menú.
Muestra la operación, la barra, el porcentaje y los archivos completados. El progreso procede del trabajo
real. B cancela la preparación; se puede retomar abriendo de nuevo el juego.
La pantalla selecciona inglés, español, portugués, francés, italiano o alemán
mediante el idioma de la consola. Para otro idioma, o si falla la consulta al
sistema, utiliza inglés. Los estados, contadores, errores y consejos siguen
la misma selección; los acentos se dibujan con los glifos originales.
No hay que copiar una carpeta `repo`, ejecutar un extractor en el ordenador
ni proporcionar manualmente firmware DSP.

La preparación actual escribe un único contenedor
`SD:/3ds/sm64ds/prepared.sm64ds` de 15.686.873 bytes. Valida la ROM completa,
escribe en bloques de hasta 64 KiB, sincroniza una vez el archivo completo y
verifica todos sus miembros antes de publicarlo. El juego lee ese contenedor
directamente; no crea 2.450 archivos en la SD. Esto reduce las operaciones de
metadatos y las sincronizaciones que hacían muy lenta la preparación anterior.

La ROM, las partidas y la caché de versiones anteriores quedan intactas. Una
cancelación o apagado no publica un contenedor parcial. Si el temporal `.part`
ya estaba completo y supera la verificación, el siguiente inicio lo recupera;
si estaba incompleto, se vuelve a generar el único contenedor desde la ROM.
Conviene dejar al menos 64 MiB libres además de la ROM para contenedor,
temporales y partidas. Al reiniciar, se comprueba la integridad del contenedor.
El tiempo físico depende de la SD y requiere una medición en la consola.

El juego continúa usando su partida integrada en `SD:/3ds/sm64ds/user-test.sav`;
la preparación no escribe ni borra esa partida. El registro de arranque queda
en `SD:/3ds/sm64ds/user-test.log`. Los controles analógicos y la cámara libre
de C-stick son los valores por defecto, también cuando no existe
`settings.ini`. C-stick requiere el hardware correspondiente; en Old 3DS
se mantienen los controles originales disponibles.

## Audio del sistema

Si ya existe un componente DSP normal en la SD o el launcher proporciona
`hb:ndsp`, se conserva ese proveedor. Cuando falta, el CIA lee el contenido
de Home Menu instalado en esa misma consola, expande su código con límites,
verifica las secciones del componente y lo suministra a libctru en memoria.
No distribuye firmware, no accede a otros procesos y no escribe NAND ni un
dump a la SD. El servicio DSP sigue validando la carga del componente.

El permiso de lectura de contenido de títulos es `CategoryFileSystemTool`,
además del acceso directo habitual a la SD. No se solicitan montajes de NAND
raw. La aplicación falla de forma visible si no puede preparar el audio.
Este acceso al Home Menu necesita su propia validación en una consola física;
una prueba de Azahar con el componente local del emulador no lo acredita.

## Memoria y vida de los recursos

- La extracción usa un único búfer de 64 KiB. No carga la ROM completa ni
  el archivo de sonido de 4,4 MiB para preparar la caché.
- Las rutas y los búferes de arranque usan un asignador explícito basado en
  `malloc/free` del sistema. El `operator new` global de este port pertenece
  al heap DS, que todavía no existe en esta fase. Las pruebas prohíben ese
  asignador global durante la preparación para detectar esa dependencia.
- La pantalla dibuja directamente en los framebuffers de libctru. Sus vistas
  de imagen y fuente tienen un dueño de arranque y se liberan antes del juego.
  No tiene hilos, texturas GPU ni asignaciones de heap por fotograma. El arte
  auténtico procede de la ROM; el CIA no incrusta esas imágenes del juego.
  Los recursos y las dos fuentes con 166 glifos usan 329.344 bytes retenidos,
  con un pico medido de 772.685 bytes en las pruebas de decodificación. El dueño de
  esos recursos libera los 329.344 bytes al terminar la preparación.
- El cierre de la pantalla limpia los dos juegos de framebuffers antes del
  renderer del juego. No inicializa de nuevo los servicios gráficos.
- La preparación automática de DSP limita el código temporal a 12 MiB y el
  componente retenido a 512 KiB. El temporal se libera antes del juego; el
  componente debe permanecer estable durante audio y sueño/reanudación y
  se libera después de cerrar NDSP.
- La caché solo se marca lista después de comprobar y cerrar todos los
  archivos. Los temporales no se consideran datos listos. FAT no garantiza
  una transacción atómica de toda la caché ante un corte de energía.

El perfil CIA usa Legacy/80 MiB (con 96 MiB el HOME Menu de la Old 3DS se
queda con 12 MiB de región SYSTEM y se cuelga al abrir el juego), CPU de 268 MHz, cores 0/1 y stack de 1 MiB,
sin forzar reloj ni L2 de New 3DS. El código de preparación es el mismo en
Old y New. Las pruebas host con ASan/UBSan verifican límites y recuperación;
la RAM máxima y la ausencia de pérdidas durante sesiones largas del juego
completo requieren medición física adicional.

## Compilación y entrega reproducibles

```sh
.venv/integrated-native/bin/python tools/build_integrated_native.py --render60 --rom-only
```

El controlador prepara un manifiesto privado y compara cada resultado con
el extractor de referencia fijado. Solo los catálogos generados se compilan;
los datos del juego se representan por sus intervalos dentro de la ROM.
Los gates host comprueban la detección de nombres libres, el descarte de
copias dañadas, la extracción, la recuperación, la pantalla y el
parser DSP antes de compilar para ARM. El candidato SD de entrega contiene
únicamente el 3DSX, sin ROM, caché, firmware, ajustes, partidas ni dumps.

`tools/package_integrated_cia.py` empaqueta exclusivamente un candidato
ROM-only cerrado y verifica el CIA completo contra su ELF exacto. El Title ID
experimental `000400000F536400` es distinto del visor histórico
`000400000F646D00`. La entrega local no publica una release en GitHub.

Los resultados host, el arranque en Azahar, la instalación mediante FBI y
el rendimiento en una Old/New 3DS física son evidencias distintas. Esta
instalación automática no demuestra por sí sola 60 FPS sostenidos.

La interfaz de arranque incluye el castillo, logotipo, fondo, estrella, marco y
tipografías del menú original. Se muestra también sin ROM o con una ROM ilegible.
Los gráficos ocupan 329.344 bytes de datos constantes y no reservan una copia
en el heap durante el arranque. Los recursos del juego siguen requiriendo tu ROM.

El logotipo se centra horizontal y verticalmente en la pantalla superior de
400 x 240, conservando sus proporciones y el fondo original del castillo.

## Preparación con la tapa cerrada

Durante la preparación se desactiva temporalmente la suspensión de APT. Cerrar
la tapa apaga ambas retroiluminaciones, omite el dibujo y permite continuar las
lecturas y escrituras. Al completar archivos y audio con la tapa cerrada, el
indicador de notificación parpadea en verde. La aplicación espera a que vuelvas
a abrir para iniciar el juego, apaga el aviso y restaura la política de
suspensión anterior. Una cancelación o fallo no activa el aviso de éxito.
No evita interrupciones por batería agotada, apagado o retirada de la SD; la
preparación desde ROM verifica el temporal completo o vuelve a generar el contenedor.
La tapa, las pantallas y el LED necesitan validación física en Old/New 3DS.

## Un solo archivo preparado en ordenador

SM64DS Packer convierte una copia compatible de la ROM en un archivo `.sm64ds`.
Cópialo, con cualquier nombre, dentro de `SD:/3ds/sm64ds/`. El port comprueba
su cabecera y cada archivo interno y monta un sistema de archivos de solo
lectura; no crea miles de archivos ni necesita conservar la ROM junto a él.
Las partidas y ajustes siguen escribiéndose normalmente en la carpeta de la SD.

El formato `SM64DSP1` contiene una cabecera de 128 bytes y los 2.450 archivos
de la revisión compatible en orden canónico, sin compresión adicional. Total:
15.686.873 bytes. Se prioriza un paquete válido; si una copia falla se intenta
la siguiente y después la ROM, si existe. Los paquetes de otra revisión o
esquema se rechazan antes de que el juego los use.
Consulta `SM64DS_PACKER.md` para los requisitos de cada sistema operativo.

## Pantalla inferior y cancelación

La pantalla inferior usa un encabezado compacto, panel y barra alineados,
texto auxiliar con espacio propio y un botón naranja inferior. Se cancela
con B o tocando el botón; en una pantalla de error se sale con B, START o el
botón táctil. El aviso de espera larga se muestra solo al escribir datos
desde la ROM (`extract`/`write-package`), no al verificar un contenedor ya
preparado, comprobar la ROM, preparar audio o mostrar listo. La selección
de texto conserva los seis idiomas del sistema.

El registro `user-test.log` incluye `Setup I/O`: fase, tiempo total, tiempo
de fase, bytes y miembros. Esos tiempos deben medirse en consola física
para diagnosticar la SD; un tiempo de Azahar no predice su velocidad real.
