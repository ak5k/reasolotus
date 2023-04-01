# ReaSolotus
* Plugin extension to improve live mixing experience with REAPER.
* Uses discrete Mix and Solo buses (tracks) to achieve non-destructive soloing.
* Also known as Cue / PFL / Listen.
* Works by capturing solo commands and managing track routing automatically.
* Works in tandem with control surfaces, mouse, and keyboard shortcuts.
* 'Hijacks' tracks routed to Main / Master, and reroutes them through Mix and/or Solo bus when soloed/unsoloed.
* Folder tracks (parent/child structure) and are currently not supported.
* WARNING: Flattens existings parent/child structures and replaces parent send with ordinary send.
* Manual routing (e.g. grouping) can be done with sends.
* REAPER Master track is used as control room / headphones output.
* Mix track is main / program / PA / stream / tape output. Freely assignable to any hardware output(s).
* Creating separate live mixing projects recommended.
* Setting REAPER default solo to not in-place recommended when working with more complex routings (Options > Solo settings).
* Tracks are soloed as PFL (post-fx).
* If no tracks are soloed then Mix is monitored.

![image](https://i.imgur.com/jSFMUHN.gif)
