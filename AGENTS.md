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

    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

```

Lo que logro con esto es, recupero el último dispositivo activo (el que ha lanzado el callback), y una vez accedo a él, puedo ejecutar los métodos propios de la clase "device", como getShortAddress, getRange o getRXPower.

Está claro, por tanto, que dentro de este dispositivo iré guardando sus valores principales: distancia, dirección, potencia, etc. 



### Ejemplos de la librería

1. Medir distancias: Sirve para comunicarme 1 a 1 con otro dispositivo. Sólo hace ranging. Sirve para medir la distancia y ya. 

2. Posicionamiento 2D. Plottea con una app de python la posición relativa de un tag con respecto a 2 anchors fijos y cuya posición conozco. No tiene un gran uso

3. Centralizar datos con 1 slave.

4. Centralizar datos con N slaves

(Más adelante explico a más sobre estos ejemplos de centralizar datos)

### Ejemplo 3: Centralizar datos con 1 slave

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



### Centralizar datos con N slaves. 

Esta es una ampliación del ejemplo anterior, adaptando al sistema para detectar un número de "slaves" desconocido. 

El control de este flujo se realiza utilizando una FSM cuyos estados te explico a continuación. 

En una vista general, los pasos que realiza el sistema son los siguientes:

1. El maestro empezará "descubriendo" a los dispositivos con los que llega a hacer ranging, y medirá su distancia con ellos (esta es la fase de "master ranging")
2. Después, pasarán a hacer ranging los esclavos. Cada uno de ellos se comportará como initiator durante cierto periodo, realizando sus medidas con todos aquellos dispositivos que tengan el ranging activado del sistema.
3. Una vez todos los esclavos han hecho ranging con aquellos dispositivos que alcanzan, deberán comenzar a ir pasando sus datos (haciendo los data reports) hasta que el maestro conozca todas las distancias medidas.


Las limitaciones del sistema son las siguientes:

1. Sólo debe haber un dispositivo "initiator" en cada instante, para que los "responders" no se vuelvan locos a la hora de recibir ranging polls.
2. Hay que asegurarse de que los slaves tienen tiempo suficiente para hacer ranging con aquellos dispositivos existentes.


Teniendo todo esto en cuenta, los estados de la FSM de este código son los siguientes:

1. Estado discovery: el maestro detecta todos los dispositivos que alcanza a medir. Una vez ha pasado el tiempo de descubrimiento, hará una transición al estado master_ranging sólo si se ha descubierto algún dispositivo que sea un esclavo. 
Todos los dispositivos que descubre el maestro son, al principio, "responders", de tal manera que el maestro pueda hacer ranging con ellos.

2. El maestro hará el ranging con todos los dispositivos descubiertos durante un ```ranging_time = 500 (milisegundos)``` .
Pasado este tiempo, el maestro apaga su ranging, y pasa a dejar a los esclavos que actúen como initiators (uno a cada vez), para que hagan sus medidas.

3. El maestro pasa a dejarle el turno a cada slave para que actúe como initiator. Este estado se llama "initiator_handoff". 
Utilizando un array con los índices de los dispositivos detectados que sí que son esclavos, se va pidiéndole a cada uno que cambie su modo de funcionamiento. 
El proceso es: le pido que cambie de modo a initiator -> espero el ack -> le dejo tiempo -> le pido que pase a responder -> espero el ack -> repito el proceso con el siguiente esclavo.
Este ciclo, por tanto, se repetirá mientras que el número de vueltas sea menor que el número de esclavos existentes.

4. Espera de Acks: Hay dos estados análogos, cuando espero el ack de que el dispositivo es responder, y el ack de cuando el esclavo es initiator.
Los trato de dos maneras distintas: Si no me llega el cambio a initiator, no es tan grave. Simplemente significa que ese slave durante ese ciclo no va a medir. 
Sin embargo, si lo que no llega es el cambio a responder, tengo un problema. Eso implicaría que existan varios initiators simultáneamente, y el sistema se volvería loco con colisiones de mensajes y temporizaciones mal codificadas.

5. Pasar a responder. Es lo que acabo de ver, me quedaré en este estado en bucle hasta que el esclavo activo realmente ha vuelto a ser un "responder" antes de pasar al siguiente.

6. Data report -> Una vez todos los esclavos han tenido su ranging_period siendo initiators, el ciclo de ranging ha terminado, y ahora comienza el ciclo de data reports.
Siguiendo el mismo procedimiento que antes, le voy pidiendo el data report uno a uno a cada esclavo, utilizando el mismo array de índices utilizado para el ciclo de ranging.

7. Tras terminar de recibir todos los reports, el maestro muestra los resultados, y el sistema reinicia, y el maestro vuelve a hacer su ranging. 






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


