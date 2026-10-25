# Entrada del juego

`GameInputLatch` separa la lectura a cada fotograma de la entrega al tick del
juego. Conserva pulsaciones breves de botones y pantalla táctil entre dos ticks;
la transición de liberación táctil se conserva porque `Stage::CheckInput` la
usa para su contador. No cambia la frecuencia de simulación ni interpola
por sí mismo el renderizado.

Los registros `GamePad`, `GameTouch` y `GameControl` mantienen los tamaños y
offsets de las fuentes originales. Se publican en las arenas BSS contiguas;
los símbolos interiores no reciben copias independientes que puedan divergir.

`tools/link_native.py` renombra con objcopy únicamente la definición del método
original `Stage::CheckInput` en una copia de su objeto. El wrapper nativo llama
a ese cuerpo original y después, si su puerta de entrada permite movimiento,
aplica la magnitud del Circle Pad a los campos que ya interpreta el jugador.
Conserva los botones remapeados y utiliza el `cstd::atan2` original. Eje vertical
de Circle Pad positivo arriba se convierte a la coordenada táctil DS positiva
abajo. La zona muerta se aplica una sola vez, en `input_poll`.

El adaptador está compilado y entra en la comprobación de enlace del núcleo.
El target general publica la entrada desde `NativeFrame::input_ready` después
de los samplers originales. El registro diagnóstico `input.log` conserva la
primera muestra consumida y los cambios de botones/toque; `title-state.log`
observa los records que procesa OV007. La adquisición 1791616229939712000
acredita el toque normal en tick 2337 y el título listo en 2810; no acredita
movimiento integrado de Player. La cámara del visor recibe C-stick;
el target general ya incorpora el grupo de cámara libre con gate CPU, pero
su funcionamiento en una partida integrada sigue pendiente. El requisito
vigente es giro horizontal y vertical continuo con C-stick en los modelos
New, conservando la colisión de cámara y la lógica de escenas del juego. La
implementación debe consumir el C-stick dentro de la actualización de cámara,
antes de resolver su colisión: publicar una pose desde `input_ready` permite
que `Camera::Behavior` la sobrescriba después. Old 3DS sigue siendo la base de
rendimiento y conserva sus controles originales, pues no incorpora C-stick.
START+SELECT permite salir del target general; START solo conserva su botón
original. La función de salida de START en el visor pertenece a ese ejecutable.

La escena original de título `dScDSMT` exige un toque nuevo o B nuevo en la
puerta de «Touch to Start», con opacidad `0x1000`, entrada desbloqueada y
estados principal/subestado cero. A pertenece a otra rama de navegación.
`build/native-runtime/title-start-test.json` compara doce casos de esa hoja
original en ROM y ARM11, con canarios y condiciones negativas. B desde el
título listo abre el modo de dibujo. La navegación de teclado exige dos
pulsaciones nuevas de A separadas por una liberación: la primera arma el foco
original cero y la segunda confirma Adventure. Su ruta desde la RAM lista
se conserva en `build/native-title-video/ram-title-ready-622993-keyboard-filemenu.json`.
El toque directo de Adventure usa el rectángulo DS [72,148,184,188], centro
(128,168). Para una ranura vacía en selección de archivo, los centros de los
elementos 14/15/16 son (44,68), (128,92) y (212,68). El replay normal de la
primera termina en `StartFile(1,0)` y petición de escena 3 sin escribir una
ranura persistente. Los replays usan copias de RAM y el payload real de FAT;
no representan entrega de estas entradas en Azahar ni inicio de partida.

El integrador portable de cámara está en `camera_orbit.hpp/.cpp`, adoptado
por el grupo de cámara del target general. Usa la LUT y los convenios angulares originales,
tiempo explícito y C-stick Q15. Sus pruebas host/ARM11 acreditan aritmética,
pausa, continuidad, límites y lifecycle por tokens. El fixture original de
Camera en Stage parcial del castillo ejecuta Behavior pausado, Behavior
activo, `func_02009e70` y otro Behavior activo. Cada llamada
activa ejecuta nueve consultas Lin/Gnd reales; la pausada ejecuta cero y aún
conserva sus efectos originales en flags/ángulos. Player y controles quedan
idénticos. El modo resuelto observado es `data_0208715c`, sin ViewObj ni target
secundario. Informes: `build/native-camera-plan/world-fixture-projection/gate.json`
y `build/native-camera-plan/CSTICK-INTEGRATION.md`.

La revisión de address points del 10 de octubre detectó un límite de ese
fixture histórico: el retorno de InitResources usaba un vptr que no despachaba
su último slot hacia Camera::Render. La ROM y el ELF corregido prueban el
offset +0x24, slot 9, Render. Se adapta ese cuerpo completo, incluidos viewport
y blend, y se seleccionan los proveedores reales de LookAt y envío GX para
el nuevo gate. Las consultas de colisión del update histórico conservan su
evidencia; aquel retorno de InitResources no acredita Render ni inicialización
completa del candidato corregido.

El grupo manual final pasa 134 casos ARM, 1.122 consultas reales de colisión
y cinco casos del bloque exacto de altura. Ejecuta el InitResources original
completo, incluida su llamada virtual a Camera::Render, y conserva cleanup y
lifecycle. El candidato se aplica antes de las consultas de colisión; el
precheck de pared conserva el bloque automático original completo cuando
necesita resolver el obstáculo. Si el intervalo de altura resulta imposible,
se recuperan el objetivo y los clamps originales. Neutral conserva los ángulos
adquiridos; pausa, cambio de identidad y vistas especiales vuelven a la ruta
original sin acumular tiempo.

El reloj usa el serial VBlank lógico del scheduler y consume cada publicación
de input, incluso si Behavior omite su cuerpo. El grupo de seis objetos está
integrado en el ELF `cd3799f6752140172893ec61b384777078c614b21d9e726b20cff8538e4f3f29`;
es idéntico al ejecutado en el fixture. Informe y límites:
`build/native-camera-plan/camera-final-gate.json` y `CSTICK-PROVIDER-GATE.md`.
La adquisición `1791624434443135000` acredita las dos A y la selección táctil
de una ranura nueva mediante HID normal, pero se bloquea tras solicitar Stage.
No acredita movimiento ni entrega manual de C-stick a una cámara de partida.

`tests/test_game_input.cpp` verifica pulsación entre ticks, liberación táctil,
filtrado de botones exclusivos de 3DS, ejes/magnitud, límites y rechazo durante
los estados bloqueados del método original. No prueba movimiento de Player.
