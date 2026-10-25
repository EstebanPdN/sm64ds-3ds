# Desarrollo y preparación de candidatos

La línea principal es el juego integrado ROM-only. El visor histórico y el
runtime experimental se conservan como referencias; no son el ejecutable que
debe usarse para comprobar estos arreglos.

## Responsabilidades

| Ubicación | Responsabilidad |
| --- | --- |
| `source/integrated_native/` | Instalación, recursos empaquetados, presentación y adaptadores mantenidos por este proyecto |
| `source/integrated_native/options/` | Modelo y dibujo de menús, ajustes, cámara, publicación de overlays y helpers de audio/almacenamiento |
| `source/integrated_native/movement/` | Propietario del movimiento, cesiones a DS y puente de geometría/contactos/plataformas |
| `third_party/sm64ds-3ds/` | Proveedor nativo fijado; no se aplican aquí los arreglos locales |
| `tools/prepare_integrated_*.py` | Transformaciones comprobadas sobre copias generadas del proveedor |
| `tools/build_integrated_native.py` | Gates, compilación ARM, auditoría y congelación del candidato |
| `tools/package_integrated_cia.py` | Empaquetado de un candidato ya congelado y verificación contra su ELF |
| `cmake/IntegratedHostTests.cmake` | Regresiones portables integradas con CTest |
| `tests/options_*`, `tests/movement_lab`, `tests/integrated_audio_save` | Regresiones de comportamiento y contratos del código adaptado |
| `build/` | Dependencias/preparación privada, fuentes generadas, logs y evidencia local |
| `dist/` | Paquetes identificados por candidato; la fecha de una carpeta no sustituye su manifiesto |

Las fuentes DS, ROM, partidas, dumps y recursos preparados se mantienen en
sus ubicaciones privadas. Las rutas del ejecutable continúan en
`SD:/3ds/sm64ds/`; no se cambia el formato de guardado ni el Title ID.

## Comprobación portable

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug \
  -DSM64DS_ENABLE_SANITIZERS=ON
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
```

La opción de sanitizers es para host y CMake la rechaza al compilar para 3DS.
Los tests que necesitan recursos, objetos originales o fuentes generadas
tienen sus propios runners; no se registran como casos portables sin sus
entradas. Los tests mantienen `assert` activo también en configuración Release.

Pruebas adicionales de los adaptadores principales:

```sh
python3 tests/test_build_tools.py
python3 tests/options_menu/test_prepare_contracts.py
python3 tests/options_menu/test_native_pause_hooks.py
python3 tests/movement_lab/run_native_bridge.py
python3 tests/integrated_audio_save/run.py --arm --bench
```

Usar para estos comandos un Python con las dependencias del proyecto.
`--arm` requiere las variables de SDK descritas abajo. El benchmark de ganancia
es un microbenchmark host: no mide FPS ni el tiempo de mezcla en consola.

## SDK y entorno Python

`DEVKITPRO` debe señalar la instalación existente. `DEVKITARM` puede indicar
una ubicación de compiladores diferente; por defecto es `$DEVKITPRO/devkitARM`.
Las herramientas mantienen `/opt/devkitpro` como valor convencional y permiten
PATH cuando no hay una instalación explícita. Un SDK explícito incorrecto falla
con un mensaje: no se sustituye silenciosamente por otro compilador.

Ejemplo, ajustando la primera ruta a la máquina:

```sh
export DEVKITPRO=/ruta/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
python3 -m venv build/python
build/python/bin/python -m pip install \
  ndspy==4.2.0 capstone==5.0.9 pyelftools==0.33 Pillow==12.3.0 numpy
```

El driver conserva la ruta del intérprete del venv. No resuelve su enlace al
Python del sistema, porque eso pierde las dependencias aisladas. No ejecutar
los gates con `python -O` ni `PYTHONOPTIMIZE`: el driver lo rechaza.

## Compilar sin emulador

```sh
build/python/bin/python tools/build_integrated_native.py \
  --python build/python/bin/python \
  --rom /ruta/privada/europe.nds --render60 --rom-only --jobs 4
```

La ROM debe ser la revisión europea admitida, comprobada por su identidad.
El build del solver utiliza además la referencia de movimiento fijada y sus
animaciones locales que describe `tools/movement_lab/port_source.py` y
`build_core.py`; su ausencia es un error de preparación, no permiso para crear
stubs. Los builds host/ARM del solver serializan su preparación compartida.

Este comando prepara y compila, sin ejecutar Azahar. La compilación completa
incluye los proveedores de juego, audio, render y controles, además de
reubicaciones y comprobaciones de la pareja ELF/3DSX. El identificador del
candidato y sus hashes quedan en `build/integrated-native/latest.json` y en
el `BUILD.json` de su carpeta. `source-evidence/` conserva las fuentes adaptadas
y las entradas del proyecto utilizadas por el candidato.

Los gates gráficos pueden reproducirse sin un dump privado mediante dos
fixtures autorados: registros, tiles, paletas y sprites deterministas. Comparan
las funciones PPU originales y adaptadas con ASan/UBSan, incluyendo casos
admitidos, rechazados y cambios de routing. Sus informes llevan
`fixture_kind=authored_synthetic`. Esta comprobación no sustituye la evidencia
histórica capturada, una imagen de juego, PICA real o una prueba física.

## Contratos de los adaptadores

- **Imágenes de overlay:** un único productor de juego y un consumidor de
  presentación. Ocultar no libera el slot leído por el worker; el consumidor
  retira la generación incluso si falla una asignación GPU. El shutdown ocurre
  después de unir al worker.
- **Movimiento:** conservar el solver mientras siga siendo su propietario.
  Cambiar de tabla de botones o entre Hybrid/N64 no borra la historia del salto.
  DS mantiene acciones especiales, interacciones y estados que requieren cesión.
- **Gráficos:** no quitar fences ni mantenimiento de caché para mejorar una
  cifra. El orden de dibujo, los propietarios y el ritmo de simulación forman
  parte del comportamiento preservado.
- **Audio:** PCM y colas siguen perteneciendo al mixer/NDSP original. La rampa
  cambia ganancia por frame estéreo; 100 % estable conserva su camino rápido.
  Las métricas detalladas son opt-in y no escriben logs desde el pump.
- **Guardado:** ajustes y cart usan escritura comprobada. En 3DS se protege el
  primario antes de reemplazarlo para evitar el borrado implícito de libctru;
  `.bak` permite recuperación. No se promete durabilidad eléctrica de la SD.
- **Preparadores:** validar todos los puntos del contrato antes de publicar
  las escrituras; rechazar árboles del proveedor, escapes por symlink y
  aplicaciones repetidas. Un contrato desconocido debe fallar de forma visible.

## Criterios de organización y rendimiento

Se aplicaron las pautas de [Code Atlas R01/R02](https://code-atlas.estebanpdn.chatgpt.site/#r01),
[A01/A02](https://code-atlas.estebanpdn.chatgpt.site/#a01),
[Q09/Q10](https://code-atlas.estebanpdn.chatgpt.site/#q09) y
[Q13](https://code-atlas.estebanpdn.chatgpt.site/#q13): responsabilidades claras,
cambios causales, conservación de contratos, fuentes identificables y evidencia
con límites. Para el hardware se consultaron
[NP04/NP09](https://code-atlas.estebanpdn.chatgpt.site/nintendo-3ds-manual#np04),
[NP15](https://code-atlas.estebanpdn.chatgpt.site/nintendo-3ds-manual#np15),
[NP18](https://code-atlas.estebanpdn.chatgpt.site/nintendo-3ds-manual#np18) y
[NP20/NP22](https://code-atlas.estebanpdn.chatgpt.site/nintendo-3ds-manual#np20).

Los contadores de operaciones eliminadas y los tiempos de tests host se
documentan como tales. El objetivo de FPS sostenidos, audio audible, progreso
y lifecycle requiere una tanda posterior de pruebas sobre el mismo candidato.
El [inventario](INVENTARIO_PORT_2026-10-21.md) distingue los arreglos de código
de esa aceptación pendiente.
