# Ensayo *Decoupling Prefail 30s 100s*

## Qué es este ensayo

Simulación de un tren de cinco vagones que circula con normalidad, empieza a degradarse y termina rompiéndose. El objetivo es reproducir una pérdida de integridad **con un periodo previo de aviso**: una fase en la que el tren todavía está unido, pero algo empieza a fallar.

El nombre resume el guion: **100 s** de funcionamiento normal y **30 s** de degradación antes de la rotura.

## El tren

- Cinco vagones en línea: el **coordinador** en cabeza y cuatro **nodos** (V1, V2, V3 y V4), separados unos 21 m entre sí.
- Cada nodo lleva un módulo UWB y una IMU.
- El coordinador recoge las medidas de todos los nodos.

## Qué ocurre

1. **Funcionamiento normal (0–100 s).** El tren arranca, acelera de forma suave y recorre un trazado con curvas. Todos los vagones van unidos y el coordinador recibe a los cuatro nodos en cada ciclo.
2. **Degradación (100–130 s).** El enganche entre **V2 y V3** empieza a ceder. El tren sigue unido, pero el bloque trasero se va quedando atrás muy poco a poco.
3. **Rotura (~130 s).** El enganche se rompe y el tren queda dividido en dos bloques: **coordinador–V1–V2** delante y **V3–V4** detrás.
4. **Separación (130–215 s).** El bloque trasero se queda atrás y la distancia entre los dos bloques crece cada vez más rápido.
5. **Pérdida de comunicación (~215 s en adelante).** El bloque trasero queda fuera de alcance y el coordinador deja de recibir a V3 y V4. Hasta el final del ensayo solo se reciben V1 y V2.

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

`prefail_completos.csv`: una fila por mensaje recibido, durante unos 400 s de ensayo.

## Observaciones

- **La degradación es muy pequeña.** El retraso del bloque trasero durante esos 30 s es de apenas un centímetro, mucho menor que el error de la medida UWB. En las distancias, la potencia y la IMU, esa fase no se distingue del funcionamiento normal. Solo se aprecia en la posición exacta que calcula el simulador.
- **La rotura sí es visible.** La IMU la detecta en el mismo instante, porque el bloque trasero deja de acelerar con el resto. La distancia UWB la muestra unos segundos después, cuando la separación supera el error de medida.
- **Mismo inicio que el ensayo *Decoupling W2 150s*.** Los dos ensayos comparten el mismo arranque simulado y son prácticamente iguales hasta los 100 s.
- **El archivo se abrió con Excel en configuración española**, que alteró los decimales de muchas columnas. No debe volver a abrirse ni guardarse así.