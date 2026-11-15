# Geometría residente y paleta de transformaciones

Estado: implementado y comparado en BUILD80–81; predeterminado en BUILD82.
La campaña Old/New emulada acepta este hito frente a la ruta anterior, con
fidelidad comprobada. El objetivo completo2× y la validación física siguen pendientes.

## Implementación y resultado actual

La ruta sustituye el stream intermedio de60bytes del experimento78. Los grupos
rígidos se preparan al cargar NativeModel, quedan residentes en GPU y liberan
la plantilla CPU transitoria. Skin y quads renuevan una sola vez sus vértices
actuales de36bytes. El plan sólo conserva rangos y uniformes; no vuelve a copiar
ni subir geometría. Matrices y bases normales se calculan en el shader de paleta.
Se mantienen las mallas originales para los efectos que requieren el shader
general, incluidos toon, tablas especulares y coeficientes fuera de signed16.
La agrupación conserva orden, material, partes ocultas e identidad de instancia.

61pruebas host con sanitizadores pasan. El laboratorio GPU120cuadros conserva
imágenes exactas en dos poses, ocho selectores, rangos dinámicos, efectos y
restauración de estado. Comprueba504bytes dinámicos por cuadro y ninguna subida
rígida, además de move/reset, límites y handles obsoletos. Evidencia local:
`resident81-gpu-validation.json`, bajo `build/native-renderer/port-research-20261029`.

Cabeza81: render activo Old7,041→5,903ms en A/B del mismo binario; New6,698→5,631ms.
Contra55 son39–40% menos, todavía no50%. Bob mejora13–16% frente al control del
mismo binario y28–33% frente55, con imágenes y captura de escena iguales.
Persisten picos de carga: intervalo máximo54,983ms Old/51,814ms New. Una corrida
por variante en Azahar; no demuestra estabilidad física ni60FPS sin bajones.

La precarga existente sigue adelantando cabeza202 e imágenes222/4101 al primer
cuadro oculto. BUILD82 instalada confirma las tres cargas en serial1; no añade
otra caché de cabeza ni adelanta la lógica/animación del juego.

Los apartados siguientes conservan el razonamiento y los contratos del diseño.

## Motivo para sustituir el experimento78

El A/B78 de igual binario en cabeza Old muestra6,992→6,799ms, mismas imágenes,
p99 7,613→7,599ms y máximo7,868→7,636ms. Frente55:9,692→6,799ms(29,8% menos)
y New9,388→6,456ms(31,2% menos). No son50% menos.

Bob78 frente68 empeora en todas las ventanas: Old6,727/7,333/6,351/5,057→
7,221/7,847/6,892/5,708ms; New6,462/7,199/6,152/4,862→
7,031/7,656/6,686/5,512ms. Persisten intervalos máximos52,736–57,172ms.
La caché de topología y el asm conservan fidelidad, pero no resuelven el coste
estructural.78 queda optativo; no sustituye la CIA70 entregada.

La fuente lo explica: SceneRenderer::draw sube primero quad_stream y skin_stream
(líneas~547/551), luego prepare_batches copia esas poses otra vez al plan y
update_batch_stream sube ese segundo stream de60bytes/vértice e índices. La
geometría rígida residente también se vuelve a copiar/subir. En cabeza el plan
mueve146.160bytes de vértices y10.530de índices por presentación, además de la
subida dinámica previa. Reducir llamadas no basta cuando se añade este tráfico.

## Contrato propuesto

1. Paleta GPU de ocho pares MVP/base normal, con vértice de36bytes: prefijo
   NormalVertex32 y selector4. Posición/color siguen calculados por el mismo
   shader. Ninguna matriz se reconstruye desde otra ni se redondea previamente.
   Las filas MVP ocupan c0..31, UV/tint c32..34, luces/material c35..46, bases
   normales c47..70 (las primeras24filas del banco shininess). Constantes iguales
   al shader general, comprobadas por IDs/datos como en75.
2. El shader corto de paleta sólo admite iluminación sin tabla shininess efectiva
   ni toon. La ruta general conserva esas funciones y el resaltado con la misma
   geometría original. Gris sin lookup puede seguir usando el cuerpo común.
   Nunca omitir/reemplazar efectos para admitir un lote. Una negativa de admisión
   no debe envenenar el plan ya válido; termina el lote y conserva el dibujo.
3. Geometría rígida: preparar grupos residentes de hasta ocho partes consecutivas
   durante NativeModel::upload, asignando un selector estable por parte. Mantener
   un rango indexado por parte y dibujar juntos sólo rangos contiguos visibles y
   compatibles en material/textura/luces/pipeline. Cambios de estado, visibilidad,
   malla o grupo rompen la unión; no reordenar transparencias ni incluir huecos.
   Cada draw recibe las matrices de su instancia por valor en comandos GPU.
4. La primera implementación debe conservar los recursos originales para UV
   normal/fija/tablas y rollback. Unir dominios UV de distintas partes puede
   rechazar erróneamente una resolución antes válida; no reutilizar un dominio
   de grupo para afirmar compatibilidad de la parte original. Considerar unificar
   formatos después, sólo con pruebas explícitas de dominio/rango. Si faltan
   slots/presupuesto para el grupo adicional, conservar la ruta original completa.
5. Skin/mesh: ampliar la única subida dinámica necesaria con el selector asociado
   al rango de cada ocurrencia. Sus32bytes de posición/UV/color/normal se renuevan
   de la pose actual. No subir32bytes, leerlos de memoria lineal y subirlos de nuevo
   como60bytes. Usar grupos acotados/rangos estables; partes que requieren shader
   general siguen teniendo una representación original válida. Las dos instancias
   de un actor nunca comparten un destino de pose mutable en el mismo cuadro.
6. Quads: estudiar la misma subida única para cuádruplas nativas y agrupación
   adyacente. El censo de cabeza contiene51barreras de quads y59quads en runtime;
   hoy permanecen fuera de prepare_batches. No se presupone que sus materiales
   sean compatibles ni se cambia su orden, clip, pantalla o forma de mezcla.
7. Acotar memoria GPU/CPU, hacer cargas sólo bajo el fence del dueño y liberarlas
   con NativeModel. No mantener una segunda caché CPU de1MiB si la plantilla ya
   reside en GPU. Retener la ruta78 sólo como referencia optativa durante el A/B;
   quitarla después si es sustituida, no acumular caminos predeterminados lentos.

## Pruebas y aceptación

- Ejecutar el código f24 real para los ocho selectores, pares MVP/base diferentes
  con igual MVP, extremos signed16, UV/tint/perspectiva y luz especular sin tabla.
- En GPU: dos instancias, matrices distintas, parte oculta intermedia, dos poses
  cambiantes, efectos que obligan volver al shader general y restauración de su
  tabla. Prefijos, selectores, rangos y capturas frente al renderer anterior.
- Destrucción/recreación de slots, mover dueño, presupuesto agotado en mitad de
  carga y rollback sin perder los recursos visibles. Ningún puntero a captura
  o banco fuente sobrevive a su lease. No mutar un VBO rígido residente por pose.
- A/B del mismo binario en cabeza y Bob Old/New secuenciales. Incluir coste de
  preparación, upload, submit, trabajo activo completo, p99/máximo e intervalos.
  Verificar bajada de bytes subidos, no usarla como sustituto de medir ms.
- Comparación final contra55 con los CSV congelados. Exigir >=50% menos coste
  activo en ambos escenarios/perfiles conservando imagen/lógica/funciones.
  Publicar CIA por un hito aceptado y comprobar consola física separadamente;
  ningún éxito en Azahar prueba rendimiento físico ni ausencia de bajones.
