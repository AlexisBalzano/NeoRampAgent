# NeoRampAgent_
Ramp Agent interface for *NeoRadar* ATC Client <br>
<br>

# Installation
- download the latest release from the **Releases**
- Place the `.nrplugin` inside the `Plugins` folder of your NeoRadar installation
- Activate the plugin in NeoRadar settings

# Configuration
- Navigate to `Documents/NeoRadar/packages/LFXX/systems/`
- Add to `list.yaml`:
    ```yaml
    - name: ARRIVALS
  style:
    background:
      bodyOpacity: 0
      headerOpacity: 0.5
      bodyBlur: true
      headerBlur: true
      rowColor: ["=focusedAircraft"]
    headerTextSize: 10
    headerOnlyBorder: true
  filterExpressions:
    ["AND", ["$isArr"]]
  columns:
    - name: stand
      width: 40
      tagItem:
        itemName: plugin:NeoRampAgent:TAG_STAND
        leftClick: plugin:NeoRampAgent:ACTION_StandMenu
    - name: c/s
      width: 75
      tagItem:
        itemName: callsign
        color: |
          =colourInboundTransfer
          =colourOutboundTransfer
          =colourAssumed
          =colourConcerned
          =colourUnconcerned
        leftClick: callsignMenu
    ```
- And for every `label.json` inside the subfolders of systems:
    - add to arrival:
  ```json
  {
    "itemName": "plugin:NeoRampAgent:TAG_STAND"
  }
  ```
   - add to departure detailled:
  ```json
  {
    "itemName": "plugin:NeoRampAgent:TAG_STAND"
  },
  {
    "itemName": "plugin:NeoRampAgent:TAG_REMARK"
  }
  ```

# Usage
- Once loaded and connected to network, the plugin will automatically fetch and display stands
- You need to be connected as an **ATC** to be able to send data to *Ramp Agent API* & manually assign stands

# Commands
Available commands to interact with the plugin:
- `.rampAgent version`: Display the current version of the plugin
- `.rampAgent menu <ICAO>`: Change stand menu ICAO to specified airport.
- `.rampAgent dump`: Dump latest report to log file
