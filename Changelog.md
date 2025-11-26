# Changelog
Todas las modificaciones relevantes de este proyecto se documentarán aquí.

El formato está basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

## [Unreleased]
- Comprobar funcionamiento sobre N slaves, y adaptar el escalado.


## [v1.0.1] - 26/11/2025

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
