# Posicionamiento 2D:
<img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" width="40" height="40"/><img src="https://raw.githubusercontent.com/devicons/devicon/master/icons/python/python-original.svg" alt="python" width="40" height="40"/> 


### Objetivo:  
 Con este ejemplo, se pretende graficar en tiempo real la posición relativa entre el tag y los dos anchors. 
Para hacerlo, se lanza un programa en python que recibe las distancias entre el tag y los anchors.  
Dicho programa de python es el obtenido de la [Demo de makerFabs](https://www.instructables.com/ESP32-UWB-Indoor-Positioning-Test/)


### Hardware utilizado: 
1 Tag y 2 Anchors.  
La comunicación entre las 3 placas se realiza siguiendo el mismo proceso visto en el ejemplo de medir distancias. 

### Funcionamiento
La principal diferencia es que ahora se utiliza el módulo de WiFi de las ESP32.  
Concretamente, se conectará el Tag al WiFi. Esto es porque el objetivo es graficar su posición con respecto a los anchors, los cuales considero fijos.  
Además, el tag recibe la distancia a los dos anchors, mientras que cada anchor sólo conoce su distancia con respecto al tag.  
Para hacerlo, primero hay que modificar la cabecera del programa del tag, para poder realizar esa conexión:  

```C
const char *ssid = "ssid"; 
const char *password = "password";  
const char *host = "IPv4";  // CMD --> ipconfig --> leer IPv4
WiFiClient client;
```

Una vez conectado, se utilizan las funciones declaradas en los archivos link.h y link.cpp para mandar en cada vuelta del loop, los datos por WiFi al PC.   
Posteriormente, desde el programa de Python , se leen esos datos y se decodifican para obtener las distancias medidas.

### Resultado: 
El resultado gráfico obtenido es el siguiente: 

<img src="https://github.com/jimmyperezp/TFG_UWB/blob/main/03%20-%20Posicionamiento%202D/foto%20app%20posicionamiento%202D.jpg" alt="Imagen app python posicionamiento 2D" width="700">


El tag (representado como círculo azul) se moverá a medida que desplazamos la placa que actúa como tag.  
Para unas medidas correctas, se deben tener en cuenta las siguientes *consideraciones*:
1. Los anchors deben situarse a una distancia de 3 metros.
2. Se debe calibrar los anchors antes de lanzar el programa para aumentar la precisión. 
3. Funcionando en distancias pequeñas, las medidas tienen mucho ruido y presentan menor precisión. 



<br><br>




-------------
Autor: Jaime Pérez  
Última modificación: 31/08/2025  
