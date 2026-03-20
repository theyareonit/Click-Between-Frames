# v1.5.0

* Port to GD 2.2081
* Switch to using Robtop's input handling to simplify the mod & improve compatibility
* Remove "Right Click P2" option (not feasible to implement atm)
* Remove "Late Input Cutoff" option (not feasible to implement atm, also it just wasn't good to enable to begin with)
* Remove "Thread Priority" option (irrelevant now)
* Improve input precision when Physics Bypass is enabled
* Add "Precision Fix" option on Windows to fix framerate desync issues that somewhat affect input precision (should not affect physics)
* Remove winelib dependency on Linux
* Build Linux input program with an old version of glibc to (hopefully) improve compatibility

Hello, I don't have a Discord server to make announcements in so I thought I'd write something here about this update. First, I'm sorry if it crashes your game or doesn't work properly. I release updates when none of the people I ask to test the mod experience any issues. I don't have a huge sample size of testers, so I can't catch every issue (it's especially hard to get testers for Mac/iOS).

Second, I know that this update took a while to release. Well, part of that was the fact that I changed a lot of stuff which took some time. But the main reason is just that I almost never play GD, so I don't have any need to update the mod for my own sake, and at this point, I have very little personal attachment to the game. I also don't make very much money off of the mod, in fact up until very recently I had only received a single donation since 2024. I'm not going to say the total amount I've received but it's less than Doggie gets from an average Grief stream. That said, I'm sure several other modders are in a similar situation, so I'm not going to beg for money *too* much (only a little).

Also, the aspect of programming that involves reading documentation, having to look up how to do stuff, having to work with obscure libraries or language features, etc. is extremely boring to me. I enjoy the puzzle solving aspect of coming up with algorithms or trying to improve the performance of software, but anything that involves working with code that someone else already wrote is very painful. So, the idea of having to update the mod to work with a new version of Geode and a new version of GD (with its own input handling that I had to figure out and work around) very much put me off.

**On the subject of RobTop's CBS:** \
I have no issue with RobTop using the mod concept, using a similar name, etc. In fact, even if he wanted to use the mod's code directly, I wouldn't have an issue with it. However, I do wish he would have reached out before making the update so that I could help him with things like improving input precision or avoiding some of the bugs that were present in the original release of CBS (like some issues with buffering inputs, and not checking for inputs on a separate thread). In fact I had tried to get in contact with Rob several times before 2.208 released (through various different people with connections) but I never got a response back.

Overall I feel like it's somewhat pointless to make CBS and then only have 480TPS input precision, because that isn't enough to make it on par with CBF for top play so top players will just use CBF anyway (though I do appreciate CBF verifications being rateable now! Particularly Ashley Wave Trials (which I had WR on for a while after it released (btw) (and also I built the nerfdate))). The COS option seems fine though.

**Some history for those who might be interested:** \
Technically the idea for this mod goes back to early 2022, before I had even learned to code, but I didn't try to implement it for real until 2023. However, I had to stop working on it at the time because of some life circumstances. Then, in 2024, after I had gotten better at coding, I started over and created CBF as a project for university. I didn't expect to have to maintain it for a very long time since it was originally more of just a proof of concept than anything, but it got a lot more complex and also a lot more popular than I had expected. I had previously made the "-1 frame" mod back in 2.1 (to remove the game's 1 unnecessary frame of input lag) and while it had seen a lot of usage from top players, it obviously was nowhere near the level of popularity that CBF has reached.

I want to say, for the record, that I tried to be as neutral as possible on issues like whether CBF should be allowed on the Demon List, whether it should be allowed on ingame leaderboards, etc. and I tried to comply with requests from members of these communities wherever possible (for instance, adding the watermark in the top right of the endscreen, and making it so that CBF doesn't submit to leaderboards on rated levels). I never gave a single opinion even in private servers about whether I thought CBF should be allowed on the Demon List until after it had actually been allowed. So, don't blame me if you were upset with that decision.

# v1.4.6

* Fix Click on Steps processing inputs before spawn triggers were updated
* Fix Physics Bypass respawn lag
* Remove Click on Steps watermark since it should in theory have 100% parity with vanilla now

# v1.4.5

* Fix inputs not registering on some 32-bit Android devices
* Fix Click on Steps triggering anticheat on some levels
* Improve rotation accuracy on frames where the player makes an input (Windows only for now)
* Fix rotation when using "2.1 mode" Physics Bypass

# v1.4.4

* Fix dual desync (oops)

# v1.4.3

* Fix dropped inputs (oops)

# v1.4.2

* Slightly improve performance
* Fix wrong rotation calculation in duals
* Fix m_lastPosition being incorrectly set (probably fixes some bugs somewhere)

# v1.4.1

* Fix keyboard input not working on Android

# v1.4.0

* Android, macOS, & iOS support (thanks mat, Jasmine, and zmx!)
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