# ShedSolarPanel
Arduino based project to automate the solarpanel on my shed. Controlling the lights and charging a battary and delivering the leftover sunpower to the mains
Date: 02-07-2017
#Solar panel battery charger with garden lights
This project is to make the control of the 300W solar panel more intelligent.
The solar panel is charging a battery during the day, at sundown the garden lights are switched on.

When the battery is fully charged the solar panel is connected to the inverter to feed the mains.

##Used Hardware
- Victron 15/75 MPPT battery charger.
- Mastervolt soladin 600 inverter.
- LED's for the garden lights, in total 12W at 12volt.
- Arduino nano for controlling.
  - RTC clock
  - 2 serial interfaces (for victron and the soladin)

##Functional detail
The main priority of the controller is to charge the battery. Then every sunlight left is used to feed back to the 230V mains.
The Victron 15/75 battery charger is setup to switch on the load output continuously. Until the battery voltage is to low. This algorithm is controlled by the Victron.

The garden lights should go ON when the solar panel has a reading of about 12Volts. By experiments i discovered that there is just  enough sunlight left to see the pavement to walk on.

The garden lights should go ON an hour before sunrise.
The garden lights should go OFF when the Victron detects too less battery power. Or after 6 hours or sunrise.




##StateMachine

###State machine of battery charger

init

switch relay to power charger

check the battery charge state
  state should be float.
   if yes. state to, state to inverterON
   if no. state to check battery charge state.

State inverterON
  switch relay to power inverter

Check inverter is working.
  if yes. (still working), go to check inverter is working
  if no. go to state switch relay to power charger.

###State machine of garden lights

init

check solarpanel voltage sundown
  if lower than 12.0 Volt, then switch ON garden lights. State wait for sunrise.
  
check solar panel voltage sunrise
if higher than 12.5 Volt, the switch OFF garden lights.
  go to check time ON

check time ON
if time is > 6hours on, then switch OFF garden lights.
  go to check before sunrise

check before sunrise
  1 hour before sunrise switch ON garden lights.

Switch OFF garden lights
 OFF


