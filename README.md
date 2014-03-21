Mobile Tracker Client
=========

Mobile Tracker es una applicación que escanea las señales wifi que están cerca del sensor, detectando así los dispositivos móviles que se encuentran dentro de su rango.

El cliente de mobiletracker esta basado en el proyecto aircrack. Cualquier equipo linux con una tarjeta wifi que soporte el modo monitor puede actuar como cliente, simplemente hay que modificar el archivo src/tesla.cfg para indicar la dirección del servidor Mobile Tracker en el campo tesla_server.

Version
----

1.0

Tech
-----------

Este cliente funciona sobre linux.

Installation
--------------

```sh
#Ponemos la tarjeta wifi en modo monitor
ifconfig wlan0 down
iwconfig wlan0 mode monitor
ifconfig wlan0 up
#Arrancamos el cliente
./teslasensor
```
License
----

MIT


**Free Software, Hell Yeah!**

[@jjbachiller]:http://twitter.com/jjbachiller
