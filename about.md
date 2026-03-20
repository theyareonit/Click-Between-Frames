# What does it do

This mod allows your inputs to register in between visual frames, which drastically increases input precision on low framerates like 60FPS.

If you like the mod, please consider [donating](https://www.paypal.com/donate/?hosted_button_id=U2LWN9H395TF8)!

# Why not just use Click Between Steps?

Because CBS is limited to 480TPS precision, while this mod does not have a hard limit on input precision (well, it technically does, but it's an extremely large number that would be hard to calculate). Also, this mod has a few fixes to improve the precision even further than Robtop's implementation, even if you ignore the 480TPS limit of Rob's version.

# How to use

If the icon is automatically jumping when you respawn, disable the "Stop Triggers on Death" hack in Mega Hack.

To edit keybinds, go to the GD options menu and click the "Keys" button in the top right (requires Custom Keybinds).

It is recommended to use either Physics Bypass or one of these FPS values: 60, 80, 120, or 240. \
This is because 2.2 has stutters on FPS values that aren't factors or multiples of 240 unless you enable Physics Bypass.

Disable TPS Bypass/Draw Divide when using this mod, because they're pointless. \
This mod automatically overrides the vanilla "Click On Steps" and "Click Between Steps" options, so you don't need to worry about those.

The mod comes with its own version of Physics Bypass in the mod options (on Windows/Linux). Be warned that not all lists or leaderboards that allow CBF will consider this legit!

If on Linux, and the mod doesn't work, please try running the command <cr>sudo usermod -aG input $USER</c> (this will make your system slightly less secure).

# Known issues

- This mod does not work with bots
- Linux controller support is experimental

# Credits

Icon by alex/sincos.

Android, macOS, & iOS code based off [Click on Steps by zmx](https://github.com/qimiko/click-on-steps), used with permission. \
Android port by [mat](https://github.com/matcool), macOS & iOS ports by [Jasmine](https://github.com/hiimjasmine00).

(these credits are slightly outdated since the mod just uses the vanilla game's input handling now but oh well)