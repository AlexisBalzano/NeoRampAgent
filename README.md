# NeoRampAgent_
Ramp Agent interface for *NeoRadar* ATC Client <br>
<br>

# Installation
- download the latest release from the **Releases**
- Place the `.nrplugin` inside the `Plugins` folder of your NeoRadar installation
- Actiavate the plugin in NeoRadar settings
- Add tag item to your layout `plugin:NeoRampAgent:TAG_STAND`

# Usage
- Once loaded and connected to network, the plugin will automatically fetch and display stands
- You need to be connected as an **ATC** to be able to send data to *Ramp Agent API* & manually assign stands

# Commands
Available commands to interact with the plugin:
- `.rampAgent version`: Display the current version of the plugin
- `.rampAgent menu <ICAO>`: Change stand menu ICAO to specified airport.
- `.rampAgent dump`: Dump latest report to log file