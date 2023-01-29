# eos

LED room lighting daemon

This is a flimsy daemon that accepts IR remote codes and drives PWM GPIO, that in turn manages light brightness in my room.

All this works on my Odroid M1 server, with built-in IR receiver. 

The output is amplified by my [GPIO isolated switches](https://github.com/tomek-szczesny/gpio_isolated_switches) board, so it drives my strings of LEDs, about 120W in total.
