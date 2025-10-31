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

Los archivos principales son DW1000Ranging, DW1000 y DW1000Device 

### DW1000Ranging

Este archivo de la libería es el encargado de gestionar todo el envío y recepción de los tipos de mensajes existentes via UWB. 

Además de la transmisión de los mensajes usando los métodos transmit..., otra de las tareas esenciales que ejecuta este código es la llamada a los callbacks del maestro. 
Concretamente, dentro de la "zona" de recepción de mensajes en el loop, en función de cuál sea el tipo de mensaje recibido, lanzará uno u otro callback declarado dentro del código del maestro. 

Es, consecuentemente, la parte más importante de la librería, y donde estoy realizando la mayoría de los cambios. 


### DW1000

Aquí están todas los métodos y definiciones propios al chip. Se ha elaborado utilizando el user manual del chip, que se encuentra en la siguiente URL: 

https://www.sunnywale.com/uploadfile/2021/1230/DW1000%20User%20Manual_Awin.pdf 

Aquí se ponen a 0 ó 1 los bits adecuados para cada configuración, modo de uso, etc. 

### DW1000Device

A lo largo de todo el código, se gestionan los dispositivos existentes como objetos de DW1000Device. 

Sirven para mandar el shortAddress, estado, etc de cada dispositivo con el que me comunico. muchas veces, los callbacks que mencioné antes (y que lanza el DW1000ranging) llevan uno de estos objetos "device" como parámetro. 
Esto es de gran utilidad, puesto que desde el código en el que se lanza el callback, puedo acceder a los datos del emisor del mensaje, puesto que su "device" es el objeto recibido por parámetro. 

Por ejemplo, dentro del código de newRange, lanzado cuando el maestro recibe un range report, encontramos las siguientes líneas: 

```c++

    uint16_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddress();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

```

Lo que logro con esto es, recupero el último dispositivo activo (el que ha lanzado el callback), y una vez accedo a él, puedo ejecutar los métodos propios de la clase "device", como getShortAddress, getRange o getRXPower.

Está claro, por tanto, que dentro de este dispositivo iré guardando sus valores principales: distancia, dirección, potencia, etc. 



### Ejemplos de la librería

1. Medir distancias: Sirve para comunicarme 1 a 1 con otro dispositivo. Sólo hace ranging. Sirve para medir la distancia y ya. 

2. Posicionamiento 2D. Plottea con una app de python la posición relativa de un tag con respecto a 2 anchors fijos y cuya posición conozco. No tiene un gran uso

3. Centralizar datos (CD). Este es el importante. 
    Este es el que uso para lograr la centralización que se explica antes. Este código es el que se le sube a las placas.
    A continuación, haré un apartado comentando únicamente los códigos existentes dentro de este ejemplo. 





### Ejemplo 3: Centralizar datos

Este es el código importante. Es el que le subo a las placas para realizar la tarea de centralización que ya he explicado antes. 
Concretamente, en este ejemplo existen 3 archivos distintos: 

1. Master.ino
2. Slave.ino
3. Tag.ino

Lógicamente, cada uno de estos es el que le subo a cada tipo de dispositivo del sistema. Voy a explicar cada uno a continuación: 

#### Master.ino: 
Es el código del maestro. Controla utilizando una máquina de estados la situación del código en todo momento.   
El proceso es: espero a detectar slaves activos --> El maestro hace ranging --> Cambia al esclavo a modo "initiator" y le deja medir --> lo vuelve a poner en "responder" --> le pide el data report --> el maestro reinicia el ciclo de mediciones, y vuelve a comenzar.   


#### Slave.ino

Tiene un abanico de recursos muy limitado. Solo atiende a llamadas de mode switch, y cambia la placa al modo solicitado. 
Una vez realizado, envía de vuelta al maestro un ack diciéndole que el cambio ya está hecho. 

Estoy teniendo problemas en este código, puesto que veo que el slave hace el cambio a initiator sin problema, pero nunca llega a realizar mediciones con el tag. No sé qué está pasando, pero eso me fastidia el funcionamiento. 


#### Tag.ino

Este es muy simple. No tiene realmente nada, es soimplemente una placa en modo responder, que se limita a hacer ranging con aquellos dispositivos que se lo soliciten. 





## Mejoras pendientes del código

### Resultados observados
Cuando ejecuto los códigos de medir distancias, en los que simplemente se hace ranging entre 2 dispositivos 1 a 1, logro medir distancias de hasta 150 metros. 
Sin embargo, cuando hago la centralización de los datos, entre maestro y slave solo llego a medir 8 metros, y con el tag 21.

Creo que eso se debe a que el esclavo le tiene que mandar al maestro el data report, y eso es un mensaje muy pesado. 

Además, ahora mismo, consigo que el código funcione, pero únicamente si existen 1 maestro, 1 esclavo y 1 tag. Falta optimizar y escalar el código para prepararlo para más dispositivos de tipo slave. 


- Gestionar la lógica para activar las banderas y modificar los estados cuando existe más de un esclavo. 
- Optimizar los payloads, sobre todo del data report, para obtener mensajes más rápidos y que permitan trabajar a mayores distancias sin pérdidas ni desconexiones.



## Instrucciones para código nuevo 

- Todas las variables que crees deberán ser snake_case.
- Añade una cabecera en cada función explicando qué hace


