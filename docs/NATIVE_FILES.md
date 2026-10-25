# Carga de recursos del núcleo original

`source/ctr/game_files.cpp` reemplaza el acceso a tarjeta DS que realiza `SharedFilePtr::Load`, conservando los métodos originales de adquisición y liberación de referencias. El contenido procede de `AssetPack`, creado a partir de la ROM del usuario en `/3ds/sm64ds/rom.nds`.

## Interfaz y propiedad

`GameFiles` ofrece tres operaciones explícitas:

- `read_path`: ruta NitroFS exacta presente en el paquete.
- `read_fat`: FAT ID de la ROM que produjo ese paquete.
- `read_source`: FAT ID de la región del código fuente, admitido únicamente después de un binding explícito a una ruta mediante `bind_source_file_id`.

El constructor recibe un `AssetPack` ya abierto que debe permanecer vivo y sin ser reemplazado mientras se utiliza el servicio. La instancia se instala con `bind_native_game_files`; no se utiliza concurrentemente desde varios threads. El dominio predeterminado es `ExplicitSourceBindings`. Seleccionar `CurrentRomFat` exige que quien inicializa el juego haya traducido previamente sus handles al FAT de la ROM actual.

Los datos RAW se leen completos; los que llevan el prefijo `LZ77` se validan y descomprimen como LZ10. El decodificador acotado rechaza truncamientos, retroreferencias anteriores al comienzo y tokens que exceden el tamaño declarado. El límite predeterminado de salida es 16 MiB por archivo. Un fallo no sustituye el buffer de salida del llamante. No hay una caché adicional persistente de los payloads; los métodos de referencia originales conservan la propiedad del archivo cargado.

Solo en la compilación 3DS se exporta `_ZN13SharedFilePtr4LoadEv`. El ABI deriva del método original: `u16 fileID` en `+0`, contador en `+2`, puntero de 32 bits en `+4`. El reemplazo publica `data_0209d3bc`, carga el archivo, asigna mediante `Memory::Allocate(size, 32)` y publica `filePtr` después de completar la copia. No modifica las referencias. El método original `SharedFilePtr::LoadFile` incrementa el contador y el `Release` original acaba llamando a `Memory::Deallocate` al llegar a cero.

El proveedor original `src/_ZN13SharedFilePtr4LoadEv.cpp` debe quedar excluido cuando se enlaza este reemplazo. No deben coexistir dos definiciones del método. La asignación sigue dependiendo del heap nativo integrado en el núcleo; este módulo no inventa un heap válido ni reemplaza silenciosamente `Memory::Deallocate`.

## IDs de región: evidencia inicial y catálogo recuperado

Hay dos espacios de IDs antes de llegar a una ruta:

1. El handle lógico de overlay 0, que recibe `LoadFile(int handle)` o el constructor de `SharedFilePtr`.
2. El FAT ID resuelto, que se almacena en `SharedFilePtr.fileID` y recibe esta interfaz de carga.

No son intercambiables. Tampoco lo son los números de las regiones EU y US.

Se inspeccionaron estas fuentes locales del commit upstream `6230b2996636e3fd0e03cb178161c94cb4b73cc9`:

- `upstream/tools/asset_catalog.py`: el catálogo EU se genera leyendo la FNT/FAT y la tabla de nombres de overlay 0 de una ROM. La herramienta declara tabla en `0x020BD4B8`, con `0x80A` entradas, y requiere leer cada puntero a una ruta para obtener el mapping. **El script no incorpora los nombres ni IDs resultantes.** Sus salidas `build/assets/files.tsv`, `handles.tsv`, `NitroFileId.h` y `AssetHandle.h` no están presentes en este checkout.
- `upstream/port/hal/fs.cpp`: el port de PC carga esos catálogos generados en ejecución. No contiene una tabla completa alternativa.
- La copia local de SM64DSe en `build/game-data/sm64dse/src/core/NitroROM.cs` declara las distintas longitudes —2.058 handles EU y 1.824 US— y vuelve a leer las rutas de overlay 0 para construir su tabla. No trae un catálogo completo fijo EU→US; la ruta opcional `filenamesWhole.h` se refiere a un archivo externo del proyecto de ROM editada, ausente aquí.

En la investigación inicial faltaba el listado verificado de cada handle/FAT ID EU asociado a su ruta NitroFS; la actualización siguiente lo recupera de la ROM europea aportada. Las direcciones de símbolos y los nombres de funciones no lo reconstruyen. Tampoco basta la tabla US extraída de la ROM disponible: su orden y su longitud son distintos. No se requiere sustituir ni alterar la ROM US para usar un catálogo de rutas verificado; el loader ya puede resolver las rutas contra su propio FAT.

Sí existe un subconjunto comprobable de dos archivos en `upstream/port/tests/smoke_fs.cpp`, con IDs afirmados por código y expectativas del payload:

| Handle lógico EU | FAT ID EU | Ruta | FAT ID de la ROM US probada |
|---:|---:|---|---:|
| 0 | 1626 | `MG/casino_back.bmd` | 1582 |
| 2 | 1628 | `MG/d_2d_blue_shadow_nsc.bin` | 1584 |

`eu_fs_baseline_bindings` publica exclusivamente estos dos bindings por ruta. El test los aplica con `bind_source_file_id`; no se activan a escondidas ni se extrapola su diferencia numérica a otros recursos. No constituyen una tabla suficiente para el menú o la aventura.


## Catálogo europeo verificado el 9 de octubre

Se encontró en la raíz el ZIP europeo aportado y se extrajo una copia privada
como `build/roms/europe.nds`, sin modificar el ZIP ni la ROM USA. Es ASMP revisión
0, SHA-256 `ce5829aaa79f06c67140ed1f45cbca738b0a9d0180538c8c1498472d859db032`.

`tools/game_file_catalog.py` usa el lector del checkout upstream fijado para
validar los 2.058 punteros del catálogo EU contra sus rutas FNT/FAT. Hay 2.072
archivos con nombre. La comparación por ruta exacta con ASME0 da 1.815 handles
con correspondencia y 243 sin archivo homónimo; 1.738 payloads coinciden byte por
byte. Comparando EU consigo misma, los 2.058 handles y payloads coinciden.
Ningún ID ausente recibe un fallback numérico.

Los informes privados son `build/game-data/eu-file-catalog.json` y
`eu-file-catalog-identity.json`. Se generan con Python + ndspy:

```sh
python3 tools/game_file_catalog.py build/roms/europe.nds 'Super Mario 64 DS.nds'
```

El catálogo elimina la ausencia de evidencia EU; todavía hay que integrarlo en
la inicialización de handles del núcleo y resolver los recursos regionales
faltantes. **El CIA actual sigue aceptando únicamente el perfil ASME0 para sus
constantes de arranque.** Encontrar y verificar la ROM EU no activa su soporte
completo ni ejecuta el menú.

## Pruebas realizadas

`tests/test_game_files.cpp` construye una ROM sintética, la importa mediante el importador real y comprueba RAW, LZ77, límites, errores de retroreferencia, binding repetido/conflictivo y rechazo de ID desconocido sin fallback numérico a US. También confirma que un error preserva el buffer anterior.

Con `build/assets.pak` como argumento adicional, prueba el BMD real del castillo mediante ruta, FAT y binding explícito, y compara los tres buffers byte por byte. Los dos archivos EU del subconjunto anterior se resuelven a sus rutas US y pasan las mismas expectativas independientes de upstream: BMD de 35.600 bytes con suma `0x2128ef`, y tilemap de 2.048 bytes con suma `0x48000`. El BMD también coincide en sus ocho primeros bytes.

El conjunto pasó con AddressSanitizer y UndefinedBehaviorSanitizer en host. `game_files.cpp` compiló para ARM11 con `__3DS__`, `-Wall -Wextra -Werror`. Estas pruebas no ejecutan todavía el método dentro del juego completo ni acreditan rendimiento en una consola física.

## Límites que siguen afectando al arranque

Los IDs codificados de archivos interiores de NARC (`>=0x8000`) exigen un montaje de archivo verificado. `LoadFile(int handle)` necesita la resolución lógica anterior y no puede conectarse directamente a `read_fat`. Las llamadas que escriben a direcciones DS de VRAM requieren el renderer nativo; este servicio solo carga buffers CPU. Las operaciones de overlays, threads, IRQ y caché de DS no reciben respuestas de éxito ficticias de este módulo.

## Registro ROM y lifecycle NARC en el target general

`native_card.cpp` conserva los registros originales NARC y mantiene su propia
propiedad de payloads y referencias de archivos abiertos. Los montajes usan
los parsers y registro originales; `func_02018908` retira el registro antes de
liberar su buffer, y los archivos abiertos conservan esa propiedad hasta cerrar.
La identidad ROM se refiere al paquete real ya abierto y verificado.

La RAM de la adquisición `1791624434443135000` reveló una cabeza de registro
apuntando a memoria libre y el record ROM estático vacío. El replay exacto
reproduce un enlace consigo mismo al reutilizar la cabeza para `c2d`; la
búsqueda de `cee` queda atrapada. La inicialización original coloca el record
ROM permanente antes de comprobar FAT/FNT, manteniendo un predecessor para
retirar los NARC. El proveedor nativo publicaba la identidad sin registrarla.

La corrección llama a `sm64ds_native_register_rom_archive` después de validar
el catálogo y antes de publicarlo. El helper usa los originales InitArchive y
RegisterArchiveName, el record y nombre ROM reales, y rechaza una primera
cabeza no vacía. Una llamada repetida valida cabeza, clave y predecessor sin
limpiar la cadena NARC. Unregister se conserva. No se inventan flags Loaded ni
callbacks de tarjeta: OpenFast/ReadFile de NativeCard conservan el transporte
real. El SDK de inicialización y DMA de DS completo sigue siendo otra frontera.

El candidato `candidate-rom-archive-anchor` está compilado y tiene revisión
estática aprobada. La comprobación ARM nueva pasó 17 casos de montaje, retirada,
recarga, errores y referencias de archivos abiertos; usa estado fresco y
archivos reales con transporte y allocator acotados y declarados. La
adquisición anterior no se instala sobre ese nuevo ELF. Informes y límites:
`build/native-integration-check/stage-requested-443444/CAUSAL-PROOF.json`
y `build/native-integration-check/candidate-rom-archive-anchor/build.json`.
