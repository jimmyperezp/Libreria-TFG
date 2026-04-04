# 2D Positioning script

> <img src="https://raw.githubusercontent.com/devicons/devicon/master/icons/python/python-original.svg" alt="python" width="80" height="80" align = "right"/> This python script received via Wi-Fi the distance between 1 tag & 2 anchors. It plots them in real time, leaving both anchors in fixed positions. 

<br>

### Functioning

This script uses python's simple drawing library (turtle) to represent the 3 nodes as circles.  

Knowing the distance between both anchors (which is set at 3 meters), and the distance between one of them and the tag, doing simple calculations, the positions can be obtained and drawn. 

The calculations are these: 

```c++
ef tag_pos(a, b, c):
    # p = (a + b + c) / 2.0
    # s = cmath.sqrt(p * (p - a) * (p - b) * (p - c))
    # y = 2.0 * s / c
    # x = cmath.sqrt(b * b - y * y)
    cos_a = (b * b + c*c - a * a) / (2 * b * c)
    x = b * cos_a
    y = b * cmath.sqrt(1 - cos_a * cos_a)

    return round(x.real, 1), round(y.real, 1)
```


<br>


### Results

The plot shown by the python app is: 



<br><br>
------
Author: Jaime Pérez  
Last modified: 04/04/2026

<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 