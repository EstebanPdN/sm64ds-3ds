# CIA de desarrollo

El empaquetado convierte el ELF nativo compilado en un CIA. El ejecutable actual inicia el heap original aislado, valida datos regionales, importa recursos y dibuja el castillo con Citro3D. Todavía no incluye la aventura, el menú original ni movimiento del personaje. Un CIA válido no demuestra que esas funciones estén implementadas ni acredita rendimiento en hardware físico.

La ruta de la ROM en la tarjeta SD es **`/3ds/sm64ds/rom.nds`**. El importador escribe `assets.pak` en esa misma carpeta. El paquete de distribución no contiene la ROM ni recursos del juego.

## Generación

Requisitos: ELF producido por el CMake del proyecto, Python 3 con Pillow >= 10.1, `makerom` y `bannertool`. Las herramientas deben proceder de sus proyectos originales; el script acepta ejecutables ya instalados, sin descargarlos automáticamente.

```sh
python3 tools/package_cia.py \
  --makerom /ruta/a/makerom \
  --bannertool /ruta/a/bannertool
```

También admite las variables `MAKEROM` y `BANNERTOOL`, o las herramientas en `PATH`. `--elf` permite seleccionar otro ELF y `--output` otro directorio. Hay que actualizar los textos que identifican el estado del programa cuando se integre la aventura; el nombre actual de preview es deliberado.

Salida:

- `dist/sm64ds-native-preview/sm64ds-preview.cia`
- `dist/sm64ds-native-preview/3ds/sm64ds/LEEME.txt`
- `dist/sm64ds-native-preview/verification.json`
- Intermedios, registro de comandos y CXI en `build/package/`.

La portada y el icono son gráficos de texto originales de desarrollo, y el audio del banner es silencio. El empaquetado no toma imágenes ni música de otras fuentes. Se incluye `LICENSE-upstream.txt` con la licencia del código reutilizado.

## Perfil del ejecutable

`packaging/sm64ds.rsf` declara una aplicación SD normal con memoria Old 3DS de 64 MB, modo extendido `Legacy`, CPU base de 268 MHz, afinidad al núcleo 0 y sin exigencia de L2 ni núcleo 2. El SMDH permite 3D y todas las regiones sin marcar la aplicación como exclusiva de New 3DS. El servicio `ir:rst` permite que la aplicación consulte el C-stick al detectar New 3DS; `ptm:sysm` permite las llamadas de libctru al control de CPU. El código decide en ejecución qué funciones adicionales utiliza.

Title ID de desarrollo: `000400000F646D00`. Product code: `CTR-P-SM6D`. Es un identificador homebrew elegido para este proyecto, no una asignación oficial ni una garantía de ausencia de colisiones con software externo.

El CIA usa firmas de prueba y contenido sin cifrar, como paquete homebrew para un sistema con soporte de homebrew. No contiene RomFS. Los recursos extraídos permanecen en la SD. El cambio de título y perfil de memoria no implementa por sí solo compatibilidad física.

## Verificación

```sh
python3 tools/package_verify.py dist/sm64ds-native-preview/sm64ds-preview.cia
```

El verificador comprueba límites y alineación de secciones, igualdad de IDs entre ticket/TMD/NCCH, índices y hashes SHA-256 del TMD, contenido NCCH, exheader, logo, encabezado ExeFS y sus tres archivos (`.code`, `icon`, `banner`). También verifica el perfil Old 3DS, permisos SD, indicador 3D, regiones del icono y ausencia de RomFS. No verifica firmas comerciales ni juega la aventura.

El proceso puede repetirse con los mismos argumentos. `makerom` genera un ticket con datos variables: el hash de todo el CIA puede cambiar aunque el contenido CXI sea idéntico. El informe conserva ambos hashes y el del ELF para distinguir estas situaciones.

## Prueba de instalación en emulador

```sh
python3 tools/package_smoke.py \
  --azahar /ruta/a/Azahar.app/Contents/MacOS/azahar \
  --model old
```

Para el CIA con `SM64DS_ORIGINAL_HEAP=ON`, añadir `--require-original-heap`:
el comando falla si no queda registrada la prueba nativa de asignar/escribir/
liberar y recuperar el espacio del heap original. El informe también distingue
la inicialización de datos de la ejecución de gameplay, que sigue pendiente.

Usa directorios XDG propios en `build/package/azahar-old/` y una SD aislada con un enlace a la ROM local. Instala el CIA mediante `azahar -i`, confirma que el `.app` instalado coincide byte por byte con el contenido verificado del CIA y lo arranca. `--model new` selecciona otro directorio independiente. No instalará nada en una consola física.

La prueba termina a los 60 segundos como máximo, salvo los cinco segundos de cierre del proceso. Para verificar una salida limpia, pulsa START dentro de la aplicación (tecla `M` en el perfil predeterminado) y cierra Azahar antes del límite. El informe distingue salida normal de cierre por límite de tiempo. Una notificación de macOS por ejecutar directamente el binario puede requerir aceptar su aviso informativo.

Evidencia inicial de esta sesión: instalación aceptada por Azahar 2126.1.2; arranque del `.app` instalado con perfil Old; importación de 1.940 entradas de la ROM ASME; dibujo del castillo de 1.370 triángulos; lectura del slider 3D de 0,75; salida limpia por START. El emulador reportó 120 fotogramas en 1.997 ms en el visor. Esto describe una prueba del visor en emulador, no 60 FPS del juego en una Old 3DS física. Los informes concretos y hashes vigentes quedan en `build/package/`.
