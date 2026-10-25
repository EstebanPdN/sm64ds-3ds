# Campaña de pruebas y optimización — 22 de octubre de 2026

La campaña usa un candidato cerrado por SHA-256, perfiles Azahar aislados y
CPU emulada al 100 %. Las partidas personales y el perfil habitual se conservan.
Los resultados de Azahar sirven para localizar costes y regresiones; la
aceptación de rendimiento en Old/New 3DS físicas queda registrada aparte.

## Orden y cobertura

| ID | Caso | Comprobaciones y evidencia |
| --- | --- | --- |
| BOOT | Instalación limpia, ROM y contenedor preparado | Progreso, título, dos pantallas, audio, ausencia de fallos CPU/memoria; captura real |
| UI | Título, Options, pausa y reapertura | Todas las categorías, páginas, aceptar/cancelar, volumen, Display; capturas reales de estados cambiados |
| C0/C1 | Castillo exterior, Yoshi, Old/New | Reposo, marcha, salto, cámara, HUD y tres capturas de RAM/render/traza |
| B0/B1 | Bob-omb Battlefield, Mario, Old/New | Geometría abierta, actores, sombras, direcciones de cámara y diálogos |
| W0/W1 | Whomp's Fortress, Luigi, Old/New | Geometría vertical, bordes, colisiones, plataformas y cámara |
| J0/J1 | Jolly Roger Bay, Wario, Old/New | Agua, transparencia, transición de movimiento y cámara |
| T0/T1 | Tick Tock Clock, Wario, Old/New | Interior, mecanismos, plataformas, oclusión y cámara |
| MOV | Original, Hybrid y Nintendo 64 | Marcha/parada, salto/doble/triple cuando procede, cambio de modo y cesión a DS |
| SAVE | Opciones y partida privada | Guardar, cerrar y reabrir; verificar bytes y errores; durabilidad eléctrica requiere consola |
| STEREO | Dos ojos en un escenario representativo | Imagen, HUD, cambios de modo y costes; slider físico pendiente |
| REG | Regresión de cada arreglo | Mismo escenario/configuración/cámara antes y después; diferencia de píxeles o contadores cuando sea reproducible |

En cada escenario se registra primero una referencia en reposo. La inspección
dirigida gira horizontalmente hacia ocho sectores cuando la cámara del juego
lo permite, prueba inclinación alta/baja y acerca la vista a geometría. Se
registran las direcciones realmente alcanzadas y las restricciones del juego;
una visita al nivel no se marca como cobertura de todo su contenido.

## Medidas y bajones

- Contadores por subsistema: preparación PPU, compilación de geometría,
  clasificación, texturas, envío de dibujo, composición, transferencias,
  espera del worker, comienzo/final del frame, actualización del juego,
  mantenimiento de audio y regulación de cadencia.
- Media, percentil 95, máximo y número de muestras por escenario y sector.
  Los ámbitos anidados o ejecutados en paralelo se informan por separado.
- La referencia de 60 Hz es 16,7 ms por presentación; la simulación conserva
  su cadencia original. Un intervalo superior a 25 ms se investiga y una
  repetición sostenida o un fallo visual provoca captura RAM/render/traza.
  Las capturas programadas a 10/30/60 s permiten detectar también picos previos.
- Se excluyen de las muestras estables el arranque, los cambios de escena,
  diálogos/pausa, escritura de dumps y lectura GPU. Cada exclusión tiene causa.
- Se conserva el render GPU normal. El replay por software se utiliza para
  contrastar composición/píxeles cuando ayude a explicar un fallo concreto.
- Se revisan bucles repetidos, asignaciones, copias, repintados, aperturas de
  archivo y datos/código alcanzables antes de modificar la ruta crítica.

## Registro y criterios de cierre

Los planes, logs, imágenes y dumps privados se guardan bajo
`build/integrated-native/acquisitions/`. Cada intento conserva el candidato,
modelo, configuración, entradas de prueba y motivo de cierre. Un intento
interrumpido o incompleto permanece clasificado así y no cuenta como aprobado.

El informe final distingue pruebas ejecutadas, fallos corregidos, escenarios
incompletos y validación física pendiente. Un cambio se acepta cuando pasan
sus comprobaciones de corrección y el caso afectado vuelve a ejecutarse.
