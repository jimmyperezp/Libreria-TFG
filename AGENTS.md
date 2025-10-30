# Agents.md 

## Contexto del proyecto

Estoy trabajando con unas placas ESP32 con un chip DW1000. 

El objetivo del proyecto es lograr controlar y gestionar comunicaciones por UltraWideBand (UWB) utilizando Two Way Ranging

Concretamente, estoy modificando la librería empleada para lograr distintas funcionalidades. 

## Contexto del código.

El objetivo que quiero lograr es centralizar los datos de las distancias entre los distintos dispositivos del sistema en uno de ellos: el ancla maestra. 
Las placas que componen al sistema pueden ser: ancla maestra, anclas esclavas, o tags. 

Para hacer esto, tuve en cuenta lo siguiente: 

- Para hacer ranging, se necesita un dispositivo "initiator" y otro "responder". 
- Si ponía el ancla maestra como initiator, y todos los demás como responders, sólo podré medir la distancia desde el maestro a cada uno de los demás. Sin embargo, lo que quiero es conocer también la distancia entre ellos. 
- Para solucionar el punto anterior, implementé un nuevo tipo de mensaje: el mode switch. Este es enviado por el maestro a los esclavos para que cambien de initiator a responder o viceversa. Así, logro que midan ellos con el resto de "responders", que serán los tags (los tags son responders permanentemente). 
- Una vez hecho el cambio, dejo que los anclas esclavos también midan. Pasado cierto tiempo, les tengo que pedir que envíen esas mediciones al maestro. Para hacerlo, añadí un tipo más de mensaje: datarequest. De nuevo, enviado por el maestro a los esclavos. 
- Una vez reciben este data request, lso esclavos codifican sus medidas y las mandan a través de un transmitdatareport.
- En el desarrollo de los ejemplos, vi que muchos mensajes se perdían, porque las placas estaban comunicándose entre sí con otro tipo de mensajes. Para hacerlo, también implementé un método que para el ranging del chip una vez ha terminado el range en curso. usa una bandera tras realizar el range report. 


- La llamada a las funciones asociadas a cada tipo de mensaje se realiza via callbacks. Cada tipo de dispositivo (maestro, esclavos y tags) tienen enlazados solo los callbacks que les corresponden. 

## Estructura de la librería

Los archivos principales son DW1000Ranging, DW1000 y DW1000Device. 

### DW1000Ranging

Aquí se incluye todo el envío y recepción de mensajes por UWB. Están todos los tipos vistos antes. Cuando se recibe cada tipo, se llama al callback enlazado en el código que se le sube a la placa

### DW1000

Aquí están todas los métodos y definiciones propios al chip. Se ha elaborado utilizando el user manual del chip, que se encuentra en la siguiente URL: 

https://www.sunnywale.com/uploadfile/2021/1230/DW1000%20User%20Manual_Awin.pdf 

Aquí se ponen a 0 ó 1 los bits adecuados para cada configuración, modo de uso, etc. 

### DW1000Device

A lo largo de todo el código, se gestionan los dispositivos existentes como objetos de DW1000Device. 
Sirven para mandar el shortAddress, estado, etc de cada dispositivo con el que me comunico. 
Son de gran importancia.


### Examples

1. Medir distancias: Sirve para comunicarme 1 a 1 con otro dispositivo. Sólo hace ranging. Sirve para medir la distancia y ya. 

2. Posicionamiento 2D. Plottea con una app de python la posición relativa de un tag con respecto a 2 anchors fijos y cuya posición conozco. No tiene un gran uso

3. Centralizar datos (CD). Este es el importante. 
    Este es el que uso para lograr la centralización que se explica antes. Este código es el que se le sube a las placas.
    Hay un apartado en posibles mejoras más adelante

## Mejoras pendientes del código

El último paso que logré fue realizar las peticiones de los distintos tipos de mensajes por unicast. Ahora falta por mejorar la gestión interna de las banderas. 

Lógicamente, me interesará que las peticiones y recepciones se hagan de todos los dispositivos, pero actualmente, está dimensionado para solo 1 anchor esclavo. Tendría que preparar el código para poder escalarlo con más placas, asegurándome de que todas hagan los cambios correctos. 

- Gestionar la lógica para activar las banderas y modificar los estados cuando existe más de un esclavo. 







## Instrucciones para código nuevo 

- Todas las variables que crees deberán ser snake_case.
- Añade una cabecera en cada función explicando qué hace


