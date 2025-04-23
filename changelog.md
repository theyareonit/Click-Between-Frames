# v1.4.2

* Slightly improve performance
* Fix wrong rotation calculation in duals
* Fix m_lastPosition being incorrectly set (probably fixes some bugs somewhere)

# v1.4.1

* Fix keyboard input not working on Android

# v1.4.0

* Android & macOS support (thanks mat, Jasmine, and zmx!)
* Added a "Click on Steps" mode in the mod settings
* Fixed percentages with physics bypass on 2.2 levels
* Fixed not being able to unplug/replug input devices on Linux without restarting GD (thanks keratomalacian!)
* Allow disabling "Late Input Cutoff" on Linux
* Significantly cleaned up code, added comments

# v1.3.0

* Update to 2.2074
* Enforce 240 steps/sec minimum for physics bypass, add 2.1 physics bypass mode
* Add a toggle for native Linux input detection (please click on the information button for the setting before disabling it!)
* Fix right click buffering before attempts on Windows (not fixed on Linux)
* Improve performance when the mod is disabled
* Reduce bugginess of the "Reduce Mouse Lag" option; also, it now works even with CBF disabled
* Possibly fix an issue where the mod wouldn't properly detect when the player died

# v1.2.3

* Fix "Reduce Mouse Lag" option causing the game to pause

# v1.2.2

* Add "Reduce Mouse Lag" option to improve performance with high polling rate mice
* Add "Safe Mode" option to prevent submission of progress/completions
* Possibly fix input on Linux for some people
* Add back donation link cuz Stripe finally reactivated my account

# v1.2.1

* Fix a bug that could cause input on Linux not to work
* Update warning about permissions on Linux

# v1.2.0

* Potentially reduce slope bugs
* Potentially improve performance
* Fix CBF not working for dash orb releases
* Fix keyboard input not working on Linux
* Significantly improve input precision on Linux
* Disable input when unfocused
* Don't submit CBF completions on star rated levels to leaderboards

# v1.1.21

* Fix an exploit that could allow you to double jump
* Add thread priority option

# v1.1.20

* bandaid fix for upward slopes cuz whatever

# v1.1.19

* Fix delayed inputs on blocks moving downward
* Possibly fix velocity on slopes moving downward (needs further testing)

# v1.1.18

* Fix orb void clicks

# v1.1.17

* fix lock to player x again oops

# v1.1.16

* Fix various velocity bugs caused by moving platforms
* Fix collision bug involving moving D-block platforms in wave
* Fix numpad numbers (requires num lock to be enabled!)
* Reduce indicator opacity

# v1.1.15

* fix platformer again cuz im the troller of the century

# v1.1.14

* Fix platformer because I accidentally broke it in v1.1.13

# v1.1.13

* Improve collision & rotation

# v1.1.12

* Fix ignore damage

# v1.1.11

* Fix spider

# v1.1.10

* Fix timewarp again
* Allow Physics Bypass even with Disable CBF checked

# v1.1.9

* Fix D-blocks & maybe some other collision issues for non-platformer mode
* Fix a bug involving timewarp & physics bypass when the mod is disabled

# v1.1.8

* Fix Lock to Player X desync
* Add indicator to endscreen
* Fix timewarp

# v1.1.7

* Change getModifiedDelta hook priority for better compatibility with other mods

# v1.1.6

* Added a Physics Bypass equivalent because I didn't want to wait for MH Geode 2.206
* Made step calculation formula closer to vanilla to fix some edge cases

# v1.1.5

* Future proof for physics bypass
* Fix buffer clicks for the final time.......

# v1.1.4

* fix duals oops

# v1.1.3

* Add toggle to disable the mod without needing to restart GD
* Slightly improve input precision

# v1.1.2

* Fix platformer & buffer click physics for real this time (I hope)

# v1.1.1

* it turns out the physics stuff wasnt actually fixed im just pushing this version to let you all know that that was a LIE and i just got (un)lucky while testing

# v1.1.0

* Update to 2.206
* Fix platformer & buffer click physics bugs

# v1.0.9

* Add "late input cutoff" option

# v1.0.8

* Fix bugs caused by multiple inputs on the same physics step

# v1.0.7

* Hook handleButton instead of pushButton/releaseButton to massively simplify code & fix several bugs

# v1.0.6

* Fix certain bugs involving right click

# v1.0.5

* Add toggle for right click P2

# v1.0.4

* Fix typo (very imortant)

# v1.0.3

* Fix crash with More Level Tags
* Change hook priority (PR by NyteLyte)

# v1.0.2

* Fix editor playtest crash
* Update Wave Trail Drag Fix incompatibility

# v1.0.1

* Fix memory leak

# v1.0.0

* Initial release