# Ensayo *Decoupling W2 150s*

## Qué es este ensayo

Simulación de un tren de cinco vagones que circula con normalidad y sufre un **desacople repentino**, sin aviso previo. El objetivo es reproducir una pérdida de integridad instantánea y observar cómo evolucionan las medidas a medida que los dos trozos del tren se separan.

El nombre resume el guion: el vagón 2 (**W2**) se desacopla a los **150 s**.

## El tren

- Cinco vagones en línea: el **coordinador** en cabeza y cuatro **nodos** (V1, V2, V3 y V4), separados unos 21 m entre sí.
- Cada nodo lleva un módulo UWB y una IMU.
- El coordinador recoge las medidas de todos los nodos.

## Qué ocurre

1. **Funcionamiento normal (0–150 s).** El tren arranca, acelera hasta su velocidad nominal y recorre un trazado con curvas. Todos los vagones van unidos y el coordinador recibe a los cuatro nodos en cada ciclo.
2. **Desacople (150 s).** El enganche entre **V2 y V3** se suelta de golpe. El tren queda dividido en dos bloques: **coordinador–V1–V2** delante y **V3–V4** detrás.
3. **Separación (150–255 s).** En los primeros segundos el bloque trasero se aleja despacio, por inercia. Después, la distancia entre los dos bloques crece cada vez más rápido.
4. **Pérdida de comunicación (~255 s en adelante).** El bloque trasero queda fuera de alcance y el coordinador deja de recibir a V3 y V4. Desde ahí solo se reciben V1 y V2, hasta el final del ensayo (~458 s).

## Qué datos se recogen

Cada nodo envía un mensaje al coordinador una vez por segundo, y cada mensaje contiene:

- **Distancia UWB** entre el coordinador y el nodo.
- **Potencia recibida** de la señal.
- **Aceleraciones** en los tres ejes (longitudinal, lateral y vertical).
- **Velocidades de giro** en los tres ejes.
- **Posición** del vagón (latitud, longitud y altitud), que el simulador calcula sin error.
- **Datos de control** del mensaje: instante de envío y de recepción, número de secuencia, vagón de origen, etc.

Las medidas de distancia, potencia e IMU incluyen ruido simulado, como lo tendrían los sensores reales.

Cuando un nodo deja de recibirse, simplemente no aparecen sus mensajes. Por eso, tras la pérdida de comunicación, cada segundo tiene dos mensajes en lugar de cuatro.

## Archivo

`decoupling_completos.csv`: una fila por mensaje recibido, durante unos 460 s de ensayo.

## Observaciones

- **El desacople se ve primero en la IMU.** El bloque trasero deja de acelerar con el resto justo en el instante de la rotura. En la distancia UWB, el cambio tarda unos segundos en notarse, hasta que la separación supera el error de medida.
- **Reaparición final.** En los últimos segundos del registro, V3 y V4 vuelven a recibirse brevemente. Parece un efecto del simulador y está pendiente de confirmar.
- **Mismo inicio que el ensayo *Decoupling Prefail*.** Los dos ensayos comparten el mismo arranque simulado y son prácticamente iguales durante los primeros 100 s.
- **El archivo se abrió con Excel en configuración española**, que alteró los decimales de muchas columnas. No debe volver a abrirse ni guardarse así.