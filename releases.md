# Releases

Este documento me sirve para ir comentando el estado de la librería en los distintos releases que vaya haciendo.   
El formato está basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).



## [v1.0.1] - 26/11/2025


### [Unreleased]
- Comprobar funcionamiento sobre N slaves, y adaptar el escalado.

### Added
- Librería DW1000 funcionando correctamente para 1 slave y 1 tag
- Filtrado de recepción de mensajes que no van "para mí". 
- Respuesta a blinks siempre que se lanzan.

### Fixed
- Al conectar un dispositivo nuevo a la librería, el otro slave detectaba los mensajes de éste, (sus poll acks, etc). Esto se traducía en que cosntantemente estaba marcando el protocolFailed como true. No se volvía a medir con él. (change 1)
- Respuesta al blink siempre. Así, aunque se resetee el maestro, el sistema vuelve a estabilizarse. Antes, el maestro se reiniciaba, pero el resto de dispositivos lo mantenían en su lista. Esto significaba que nunca respondían a sus blinks, y el maestro permanecía ciego.

### Changed
- Dentro de DW1000Ranging.cpp:
    1. Declarado filtro para que los responders ignoren mensajes que no son para ellos.
    2. Modificada lógica de lanzar el calback de newDevice. Primero compruebo si es nuevo. Independientemente de si lo es o no, respondo al blink.
- En master.ino, añadida lógica para eliminar los dispositivos que están inactivos. De este modo, mantengo lista actualizada con la lista interna de la librería.

### Known Issues
- No sé si debería desplegar el limpiado de la lista (segundo punto de los cambios) también en el slave y en los tags.

---

## [v1.1.0] - 05/12/2025

### Unreleased 
- Apagar el ranging de los esclavos hasta después de enviar el data report
- Rework en la logica del maestro: no darle un tiempo fijo a cada dispositivo, sino dejarles hasta que han terminado (teniendo un timeout por si tardan demasiado)

### Added
- Librería DW1000 funcionando correctamente para N slaves
- Filtro software para unicast en mensajes mode switch, data report, data request.
- Envío en el data report unicamente de las medidas activas. 


### Fixed
- Al aumentar el número de dispositivos, se llenaba muy rápido el límite de dispositivos, ya que estaba utilizando el MAX_DEVICES de la librería. He separado ese límite con el de medidas. Por ejemplo, teniendo 5 dispositivos, tenía 7 medidas; había que separarlas.
- En la función 'modeSwitchRequested' anterior, llamaba a otras funciones largas. Esto no lo debería hacer dentro de una interrupción así. Lo he reworkeado, de tal manera, que ahora simplemente levanta una bandera para hacer el cambio, y se encarga de enviar el ACK.
-  Descubrí que el maestro registraba muchas medidas inexistentes que marcaba como inactivas. Esto era por el problema de llenar el buffer de data report con medidas inactivas. (es el punto 3 de los added anteriores).

### Changed
- Dentro de DW1000Ranging.cpp:
    1. Filtro por software para mensajes por unicast.

- En master.ino, mostrado de medidas inactivas en el 'showData', separación entre MAX_DEVICES y MAX_MEASURES. Por último, cambio en el nombre de debug a debug_master, para evitar problemas de compilación en c++

- Versiones de la libería. A la anterior, la llamé 1.0.1, pero debería haber sido 1.1.0, ya que los cambios fueron significativos. Ahora, he corregido y arreglado esa notación.

### Known Issues


---


## [v1.2.0] - 12/03/2026    

MENSAJES POR UNICAST y COORDINACIÓN HUB & SPOKE FUNCIONAL 

### Unreleased 
Funcionamiento en una topología de token-passing, tanto lineal como en redes mesh. Aún estoy trabajando en pulir todos los aspectos y garantizar un funcionamiento estable.

### Added
Funcionalidad de transmitir Polling por unicast + sistema de control de una red hub & spoke completa

### Fixed
No es un fix propiamente dicho, pero se han reorganizado todos los ejemplos (si funcionaba para N slaves, para 1 siempre funciona, por lo que el de 1 se ha eliminado), y organizado el readme y las carpetas del repositorio.

### Changed

Los cambios han sido super significativos y muy variados. Para evitar ir poco a poco mencionando los cambios, voy a explicar el estado de la librería en este punto, y cómo funciona.

Actualmente, los mensajes (tanto los de ranging como los del data flow), se realizan todos por unicast.
Para ello, se emplea la función transmitUnicast, la cual se encarga de hacer las transmisiones necesarias según el tipo de mensaje que se quiera enviar.

A un nivel interno, el polling es lo más reseñable. Se ha declarado el método de hacerlo por unicast. En este caso, no hay que introducir ningún timerDelay ni nada por el estilo. Solo se envía un poll y solo se espera un POLL_ACK.

Para la gestión de los dispositivos inactivos (usando el método timerTick()), se ha mantenido la lógica anterior por si el método de ranging se declara como broadcast (por ejemplo, para hacer el discovery)

A nivel "local", el código que corre en cada uno de los dispositivos es remotamente distinto a las versiones anteriores. Solo hace falta verlo.

Principalmente, la base del funcionamiento es la misma, pero con unas prestaciones y ejecución mucho mejores ahora. 

El master hace un discovery (el cual ahora se realiza solo cada cierto número de ciclos, controlado por una constante definida en la cabecera del programa), y guarda una lista con sus dispositivos conocidos.
De ahí hace medidas por unicast con todos ellos, y una vez termina, empieza a hacer el initiator handoff. En este proceso, le hace un modeSwitch a cada dispositivo de los que conoce para que ellos midan con los que ellos ven.
Cuadno todos han hecho su propio ranging, el maestro muestra por pantalla los datos recopilados

El sistema está diseñado con una lógica de reintentos y espera para las transmisiones. (ver la máquina de estados de los archivos master.ino y slave.ino).

La principal limitación de esta versión de la librería es que está diseñada para una topología hub & spoke. Esto significa lo siguiente: 

- El coordinador (o maestro), le pasa el testigo de ser initiators a todos los dispositivos que él conoce, pero estos no van más allá. Cuando les toca ser initiators, hacen medidas con aquellos dispositivo que ellos ven (independientemente de si el maestro los ha visto o no), y al terminar, le mandan sus datos de vuelta al maestro.  
Lógicamente, la principal limitación es: ir más allá. Si hay dispositivos a los que no llega el maestro, a esos nunca les llegará la oportunidad de ser initiators. La distancia total que se puede medir con este funcionamiento está limitada al alcance de 2 conextiones (del maestro al slave, y del slave hasta lo que él pueda medir).


### Known Issues
Para lograr que no haya mucha interferencia ni choque en los mensajes, he añadido un pequeño delay (que es bloqueante) después de realizar cada transmisión. He comprobado que el funcionamiento así era algo mejor, aunque eso está consumiendo tiempo de recolección de todos los datos.

Limitación de la distancia máxima posible de medir utilizando esta topología (hub & spoke). 

Este desarrollo se ha basado en el ejemplo de hub & spoke coordination. Es muy probable que algún renaming o cambios para adaptarse a este ejemplo causen incongruencias o problemas sintácticos con ejemplos anteriores. Por ejemplo, el tipo de placa "MASTER" anteriormente era "MASTER_ANCHOR". Tal vez haya archivos anteriores que conserven esa nomenclatura.

---