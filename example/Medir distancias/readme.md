# Distancia entre 1 Tag y 1 Anchor


Primera prueba en el desarrollo del TFG. Con estos códigos verificamos el funcionamiento de las placas y los fundamentos de la comunicación Anchor-Tag

 <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" width="40" height="40"/> 

## Procedimiento: 

El primer paso es la **Calibración**. El objetivo es establecer un *Antenna Delay* correcto en cada anchor. De esta manera, se consiguen medidas mucho más fiables. 

- [DW1000 Antenna Delay Calibration](https://www.decawave.com/wp-content/uploads/2018/10/APS014_Antennna-Delay-Calibration_V1.2.pdf)

Una vez conocido el valor del *Antenna Delay* en cada Anchor, ya se puede realizar la medida de la distancia entre ambas placas. 


Este procedimiento, **paso a paso**, es el siguiente: 
1. Situar el Anchor y el Tag a una distancia conocida (Tras realizar varias pruebas, es recomendable situarlas a distancias superiores a 1m)

2. Subirle al Tag el código MD_Tag.ino

3. Subirle al Anchor el código Anchor_autocalibrate.ino
    - Dentro de este código hay indicar la distancia a la que hemos situado las placas: Hay que cambiar la variable *this_anchor_target_distance* 
    ```C
    float this_anchor_target_distance = (distancia en metros);
    ```

4. Una vez ambas placas tengan el código subido, pulsar el boton de reset para establecer la comunicación. 

5. En el monitor serie del anchor, me devolverá el *Antenna Delay* que ha medido el programa. Este es el parámetro buscado. 
6. Una vez conocido el *Antenna Delay*, le subo al anchor el código MD_Anchor.ino. 
    - Dentro de este, debo modificar el valor recién medido: Hay que cambiar la variable *Adelay*, situada al comienzo del código. Basta con ponerle el valor medido en la calibración anterior. 
    ```C
    uint16_t Adelay = (Valor medido en calibracion);
    ```

<br></br>


-------------
Autor: Jaime Pérez Pérez  
Última modificación: 29/08/2025