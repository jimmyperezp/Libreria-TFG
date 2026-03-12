# Changelog
Todas las modificaciones relevantes de este proyecto se documentarán aquí.

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