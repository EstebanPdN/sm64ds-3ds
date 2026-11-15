# Próximo prototipo: transformaciones como atributos PICA

Estado actual75: implementado como experimento **optativo** mediante
`SM64DS_NATIVE_BATCHES=1`. No es todavía una mejora aceptada. La ruta habitual
conserva el renderer70 y su precarga; no se entrega una CIA experimental más lenta.
Las propuestas y censos siguientes se conservan como antecedentes, no resultados.
El objetivo2× respecto a BUILD55 permanece intacto.

La cabeza presenta129 tramos rígidos y muchos tramos de skin. No comparten
suficientes matrices/estados para juntarlos con una paleta de traslaciones.
Los cambios de VBO por sí solos tampoco ahorraron tiempo en el A/B59. La
emisión CPU con el emisor nativo67 cuesta~9,8us por llamada en Azahar; no es
tiempo GPU. El agregado de submit de cabeza sigue rondando4,1ms.

Se propone un stream nativo por frame para juntar tramos adyacentes compatibles
con distintas matrices/bases normales, sin cambiar orden ni sombreado:

- Vértice base actual: posición, UV, color y normal/uv_source (cuatro atributos).
- Cuatro filas completas de MVP como atributos float4. Deben llegar a dp4 con
  los mismos valores f24 que los uniformes originales; no reconstruir una fila
  desde otra ni dividir la transformación en fases con redondeo distinto.
- Tres filas de base normal como atributos short3 para los casos en que los
  nueve coeficientes caben exactamente en signed16. Son11 atributos en total;
  verificar el límite y la configuración efectiva del SDK/PICA antes del lab.
- El shader reconstruye los cuatro dígitos base64 por coeficiente, con extensión
  de signo, y usa las mismas operaciones de iluminación. Coeficientes fuera de
  signed16 conservan la ruta actual exacta; nunca truncar datos para admitirlos.
- Material, textura, luz/material de iluminación, UV y pipeline deben coincidir
  para juntar tramos. Matriz y base normal sí pueden variar por vértice. El
  índice de iluminación actual incluye la base: comparar sólo las partes
  invariables, no exigir el mismo ID completo. No reordenar transparencias.
- Usar geometría/indexado nativos residentes para preparar un stream acotado,
  evitando transformación CPU de posiciones. Deduplicar vértices por parte
  donde sea posible para no repetir matrices innecesariamente. El coste de
  copia/flush y memoria GPU debe medirse: podría anular el ahorro de llamadas.
- Una instancia no debe sobrescribir datos GPU de otra en el mismo frame.
  El stream debe ser propiedad del frame y respetar la barrera GPU actual.

Pruebas necesarias antes de activar: oracle f24 del shader real (MVP, normales,
UV y color), lab de primitivas con matrices/luces distintas y captura frente a
ruta actual, presupuesto/rollback, múltiples instancias, materiales mezclados,
valores extremos y capacidad máxima. Después A/B en el mismo binario, cabeza y
Bob en Old/New secuenciales, sin capturas dentro de ventanas temporizadas. La
validación física se mantiene separada. No se afirma una ganancia anticipada.

En paralelo se debe medir si la captura de luces repite hashing/copia del mismo
LightSet por cada parte. Cualquier memo debe poseer datos por valor o limitarse
al alcance inmutable de una captura: puntero/generación no prueban identidad.


## Censo real69 después de completar la captura

El exportador ya conserva skin/mesh y estados de toon/resaltado. Cabeza en
serial1180:131 partes,93 ocurrencias,1.038 vértices de skin. Tras omitir los22
marcadores degenerados como el renderer actual, quedan149 dibujos de geometría,
51 barreras de quads y2.510 vértices únicos por parte(5.613 índices).146 dibujos
son afines y admiten tanto el candidato de atributos como preiluminación más
paleta16. En ambos casos el censo cuenta34 envíos de geometría,115 menos,
conservando las51 barreras de quads. No incluye los envíos de compositor/capas.
La igualdad del ID de iluminación NO se exige: se comparan luces/materiales
por valor y la base normal puede variar. El censo anterior perdía esa opción.

La opción de preiluminar debe producir exactamente los RGB5 expandidos aRGBA8
que usa el shader actual; no se admite aproximación de punto flotante. Un
stream propio del frame conservaría posiciones y UV, y una paleta de hasta16
MVP daría a cada vértice su matriz original. La posición se seguiría calculando
en PICA con el mismo dp4, sin transformar posiciones en CPU. Este camino
requiere un shader corto separado y un oracle entero/f24 del color, además de
las pruebas de propietario/capacidad/admisiónUV. No está implementado.

La geometría observada acota el volumen de copia mucho más que un cálculo con
el máximo teórico del arena. Debe medirse el coste de copiar/flush, paleta,
preiluminación y cambios de programa en Old/New;115 llamadas menos no implica
por sí solo duplicar rendimiento. Evidencia privada y hashes de captura/bancos:
`batch69-head-census.json`. No se modifica la referencia55 ni el objetivo50%.


Bob/serial1850 capturado70:120 partes,19 ocurrencias,330 vértices de skin,
102 dibujos de geometría tras8 marcadores omitidos.2.668 vértices únicos por
parte/5.472 índices; no hay barreras de quads en ese instante.101 dibujos
admiten ambos candidatos. Atributos:34 envíos potenciales; prelit+paleta16:33.
Se conservan orden/material/textura; la normal puede variar por pieza. Es una
muestra concreta deBob, no todas sus cámaras ni todos los niveles. El carácter
general procede de los mismos tipos/contratos de render, no de excepciones
por nivel. `batch70-bob-census.json`. No es una medición2×.


## Ruta elegida para el siguiente prototipo (todavía pendiente)

Se prioriza color entero enCPU + paletaMVP16 enPICA, por dos razones verificadas:
el censo da la misma reducción de llamadas en cabeza y una más enBob, y permite
retirar del shader afín la costosa iluminación de punto fijo. El ahorro GPU de
esto último es una hipótesis: Azahar no mide el coste físico de PICA.

1. Implementar un kernel de color que acepte LightingInput ya validado y normal
   signed10. Comprobar contra `test_lighting_shader.py::reference` y ejecución
   f24 del shader actual, incluidos producto por componente antes de suma,
   shift12/signed11, residuos20bit, especular, tabla, toon y grey. La salida debe
   ser RGB5 expandido `(c<<3)|(c>>2)`, alfa255. Así el shader unlit actual hace la
   misma normalización1/255 y tint, sin otro formato/curva de color.
2. Crear un plan de dibujos propio de cada frame después de fijar los bancos.
   Validar cada asociación/rango una vez en su dueño. Preparar un stream acotado
   con vértices/indexado nativos, copiando sólo vértices usados por parte;
   conservar posiciones/UV/triángulos y añadir selector de matriz. Nunca
   modificar colores de un VBO compartido: otra instancia puede usarlo con
   distinta luz en el mismo cuadro. No introducir referencias al juego vivo.
3. El shader corto debe usar hasta16 MVP completas con selector por vértice y
   los mismos dp4 y orden/precisión f24 del original. No transformar posiciones
   enCPU ni reconstruir matrices aproximadas. Separar la DVLE de los controles
   generales y restaurar cachés/uniformes al cambiar de programa; los registros
   de una paleta no deben confundirse con luces residentes del shader general.
4. Agrupar sólo adyacentes con material/textura iguales. Mantener las barreras
   de quads en el primer prototipo. Normal/fixedUV, highlights y cualquier caso
   no admitido siguen la ruta exacta existente sin descartar geometría. La
   admisiónUV pertenece al rango fuente: no heredar un dominio global más amplio
   por juntar mallas. La capacidad no puede dejar un lote parcialmente emitido.
5. Probar múltiples actores, cambios de escena, falta de presupuesto, overflow,
   índices/paleta, texturas y propiedadGPU después del fence; luego comparar
   GPUlab e imágenes reales. A/B en igual binario y contra55, Old/New sucesivos,
   tanto coste activo como intervalos/picos. Física todavía pendiente.

Un vértice afín de28bytes (base24 + selector/padding4) con índice16bit daría
~81,5KB en cabeza y~85,6KB en la muestraBob si el censo se materializa. Es una
estimación de payload, no presupuesto completo ni consumo medido: no incluye
plan, caché de colores, paletas, padding, recursos originales ni SDK. Se debe
medir si el trabajo adicional deCPU/copia/flush compensa los115/69 envíos menos.


## Prototipo75: conservar la iluminación en PICA

72 y73 preiluminaban exactamente cada normal en CPU. Se descartó esa ruta para
el juego: aunque redujo submit, la preparación costó5,7/4,9ms y el render total
pasó de6,99ms(control72) a10,09/9,24ms. No se entregaron como mejora.

El stream75 tiene60bytes/vértice: posición/UV/RGBA8/normal32bytes, tres filas
short4 de base normal24bytes y selector de cuatro filas MVP4bytes. Ocho cargas
de atributos cubren todo el stride. No transforma posiciones ni colores en CPU.
Una paleta contiene ocho matrices completas(c0..31); UV y tint usan c32..34;
luces/material/tablas conservan c35..78. Las luces se poseen por valor en cada
lote y sólo se unen estados compatibles adyacentes, ignorando la base normal.
Bases fuera de signed16 y UV normal/fija/resaltados mantienen su dibujo original.

La transformación normal divide coeficientes signed16 en dos dígitos base64 y
un dígito alto con signo. Mantiene el mismo shift12/signed11 antes de usar el
cuerpo de iluminación original compartido. Oráculos host ejecutan el código
f24 real para4.096bases/normales/colores, extremos signed16, shininess, toon y
gris, además de2.048selectores MVP/UV/tint. El plan comprueba propiedad de
instancias, límites, reinicio y rechazo de coeficientes que no caben.

El lab74 encontró un error de ABI entre entradas: Picasso asigna las constantes
de cada DVLE desde c95 hacia abajo. Declarar sólo cuatro constantes en la nueva
entrada sobrescribía constantes que el helper compartido esperaba en otros
registros. Las normales coincidían en la sonda GPU, los colores no.75 declara
las doce constantes en el mismo orden y el backend verifica IDs/datos en el
binario antes de permitir esta ruta. El oráculo host también exige esa identidad.
El primer lab75 usó una cabecera generada antigua al restaurar la sonda; el
backend la rechazó. Sólo el lab75b regenerado corresponde al código75.

Lab75b Old/Azahar:120cuadros; las tres capturas de288.000bytes son idénticas
(SHA25635c2510bdddf5f9a3c54bcc0c14e6404f1d35052761e9f5908ad07475f516dad).
Incluye ocho selectores por grupo, luces/tabla/toon/gris, vuelta al shader
ordinario/afín, rechazo de revisión obsoleta y de subida durante dibujo, sin
crecimiento residente.61pruebas host pasan. No hay validación física.

Cabeza75 Old:900..1100 render activo7,122ms(p99 7,833; máximo8,092), intervalo
máximo18,149ms, frente6,985ms del control72. Imagen superior840 idéntica;
abajo23píxeles deBUILD. Preparar lotes consume~2,6ms, submit~1,9ms y upload
~0,77ms: el ahorro de envíos sigue anulado por la preparación. Se mantiene
**desactivado por defecto**. A/B75 de igual binario:6,993→7,122ms medio,
p99 7,612→7,833ms, máximo7,824→8,092ms; intervalo máximo18,087→18,149ms.
Ambas pantallas840 idénticas,1.200actualizaciones por ruta,0rechazos/cargas en
la ventana900..1100. Evidencia `batch75-head-same-binary-old.json`. No se
extiende una campaña Old/New/Bob de una variante que aún regresa en cabeza.
La reducción de llamadas por sí sola no satisface el objetivo2× ni prueba una
mejora física. La caché de color CPU permanece sólo como oráculo independiente;
no participa en la ruta de juego75.


## BUILD76–78: preparar lo inmutable al cargar

76 añade medición agregada optativa (`SM64DS_NATIVE_BATCH_PROFILE=1`) en una
presentación cada240: cabeza muestra~0,65ms agrupación,~0,37ms remapeo y~1,21ms
emisión de vértices. Los relojes añaden sobrecoste; NO se usa ese frame como
A/B de rendimiento ni se suman estos tiempos a otras fases anidadas.

77 prepara topología compacta por parte mientras el banco validado está vivo.
La caché posee índices relativos y vértices únicos, nunca punteros al banco ni
poses. Se mueve/libera con NativeModel y su memoria conjunta en modelos/canvas
está limitada a1MiB CPU, más128KiB transitorios de remapeo durante una carga.
Falta de presupuesto/capacidad/memoria conserva la ruta normal. Sólo se crea
si se pide el experimento de lotes; `SM64DS_NATIVE_BATCH_CACHE=0` la desactiva.
El stream skin tiene índices identidad inmutables creados por su dueño: copia
cada pose actual por rango sin volver a remapearlos.106partes compactas y40
rangos secuenciales cubren los146candidatos de cabeza. Preparación~2,37ms;
render activo medio6,882ms vs6,993ms de control75; aún insuficiente para2×.

78 conserva plantillas finales de60bytes/vértice en la caché. Una copia por
parte sustituye miles de copias pequeñas. Dos bucles ARM nativos (ldmia/stmia)
rellenan el sufijo de24bytes de base normal+4de selector, y copian los32bytes
actuales de skin+ese sufijo, respectivamente. No calculan posiciones ni colores.
El camino portable C++ usa el mismo ABI. Los bloques asm declaran registros,
flags y memoria modificados; todo se escribe en el plan CPU propietario antes
de su única subida después de la barrera GPU. No se modifican activos compartidos.

61pruebas host pasan con sanitizadores. El lab78 recorre128plantillas NMD3
independientes y dos rangos skin durante120cuadros: compara todos los prefijos,
coeficientes signed16/selectores y las poses actuales antes de subir. La ruta
normal y agrupada generan capturas idénticas para dos poses, y ambas poses
cambian píxeles. Las tres capturas de la pose inicial también coinciden. El
objeto ARM contiene ambos bucles nativos; no se presenta como medición física.
Evidencia `batch78-gpu-validation.json`. Campaña78 terminada: misma imagen en
cabeza y pequeña mejora (6,992→6,799ms Old), pero Bob empeora6–13% frente68 en
ambos perfiles. Sigue desactivado; no se entrega como mejora. La siguiente ruta
se define en `NATIVE_RESIDENT_PALETTE_DESIGN.md`: residencia GPU de rígidos y
una única subida de poses, en lugar de mantener el stream intermedio60bytes.

## BUILD80–82: sustitución por geometría GPU residente

El experimento78 queda reemplazado en la fuente por grupos rígidos residentes y
paleta GPU de ocho transformaciones. El formato36bytes conserva el prefijo
original32 y un selector4; skin/quads se suben una vez. El plan no contiene una
copia CPU de los vértices del cuadro. Las plantillas compactas sólo viven durante
la carga; NativeModel posee los recursos GPU y los libera con su ciclo de vida.
Los efectos que no admite la paleta conservan el shader y mallas originales.

El A/B81 completo, secuencial Old/New, reduce render activo de cabeza y las
cuatro ventanas de Bob; ambas pantallas y la escena Bob coinciden exactamente.
LabGPU120cuadros y61pruebas host pasan. BUILD82 activa la ruta por defecto;
`SM64DS_NATIVE_BATCHES=0` conserva el control. Detalles y limitaciones en
`NATIVE_RESIDENT_PALETTE_DESIGN.md` y `PERFORMANCE_DOUBLE_BUILD55.md`.
