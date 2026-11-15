# Passrate Tracker

This mod tracks your passrate for various runs and shows some other neat info about your passrates.

In the bottom left of the screen while playing there will be a label that shows
- The current run \[xx%-xx%]
- Your current passrate for that run \[xx%]
- A "trend". If you're currently getting better / worse \[increasing / decreasing / steady]

# How to set the "run" / which runs get saved

<cp>The start of the current run</cp> is always set to your spawn point.
<cp>The "target" (end of the run)</cp> will default to the next start position / 100%.

The hotkeys for "previous target" / "next target" will cycle the current target through the start positions / 100%.

<cp>You can additionally set a "custom target"</cp> through the menu while pausing in a level.

The specific set of runs which have their passrates saved permanently are:
- The run from 0 to the first start position
- Runs between adjacent start positions
- The run from the last start position to 100%

<cr>All other runs will not save their passrates currently.</c> If you switch to a different run and switch back, the passrate will be reset.

# How the passrate and trend are calculated

The passrate is calculated via a form of moving average where new attempts weigh more. 
With default settings after doing enough attempts, each new attempt will represent 1/200 of the passrate. This means that it will stay relatively stable if you play consistently, but will shift when you consistently play better / worse.

If you get a lucky string of attempts, that passrate isn't going to shoot up to 80% just like that. <cp>If you want a passrate that represents your most recent attempts only,</cp> you can change the "passrate sensitivity" to a lower number.
(If you do it from within a level, it also tells you roughly how many attempts the passrate will then be based on)

<cp>If on the other hand you just want a plain average of all your attempts,</cp> you can set the "passrate sensitivity" to 1.

<cp>The "trend indicator"</cp> (increasing / decreasing / steady) just uses another more sensitive moving average. If this more sensitive passrate is 5 percentage points better / worse than the shown passrate then it gives you the trend (increasing / decreasing), otherwise it displays "steady".

<cr>If you change the "passrate sensitivity" you should also change the "passrate trend sensitivity"</c> so that it stays more sensitive (If you want to use the trend indicator).

# Additional features

## Some other important features are:
- <cp>A grace period for attempts</cp>

If the attempt is shorter than this it won't be counted, to avoid quick and unfair deaths.

- <cp>A custom ending percent </cp>

If you restart from here it will count as if it were to 100%.

- <cp>The info screen (from the menu when paused in a level)</cp>

How many attempts you would have to do from 0, to have a 50% of having beaten the level.
(For example, if the level has 3 sections and you have a 50% passrate on all of them, then it's a 1/8 chance of you beating the level. For a 1/8 there is a 50% you will have beaten the level after 6 attempts. This doesn't account for nerves obviously)

How high of a passrate you need for each part to beat the level on average every 1000 attempts.
(Obviously you could also have higher passrates on some parts, and lower on others, this is just a rough guide for where you need to get your passrates)

- <cp>Resetting a segment (from the menu when paused in a level)</cp>

If a segment has a passrate of 20%, but you had a breakthrough and started beating it 80% of the time, it's going to take a while for the passrate to readjust. Hitting the "reset segment" button will make the passrate for this run very sensitive again so it can adjust.

- <cp>Disabling the tracker</cp>

From the mod settings or from the settings while playing a level you can enable / disable the tracker. Disabling will remove the display and stop all tracking.

- <cp>Deleting old data</cp>

From within a level you can also check a level as "whitelisted". Then in the mod settings there is a button to delete data for all levels that are <cr>not</c> whitelisted. The save file for the mod should be quite small anyway, but you still have this option if you want it.

- <cp>Backups</cp>

If anything ever goes wrong (like accidentally deleting data for a level you didn't want to) the save folder for the mod also keeps backups from your 10 previous launches of the game.


# Additional notes about usage

## Percentages
<cp>The mod uses the old system for percentage</cp> and currently doesn't support the new 2.2 system. It is therefore recommended that you use the "Percentage Toggle" mod by ZSA if you want the percentage in game to match the mod perfectly.

## How to interpret passrates
It is important to remember that the passrates shown can only ever be an estimate and will almost always be biased in some way.

For some people, playing a specific part in isolation will give them a high passrate, but when they play that part from 0 they get mindblocked.

For other people (like me) playing a part in isolation makes them overthink it, and their actual passrate is quite a bit higher.

This is another reason for why the "How high of a passrate you need to beat the level on average every 1000 attempts" info should only be used as a very rough guide.

## Small changes in passrates have huge benefits
If you play a level with 15 segments (14 start positions) and every part has a 60% passrate, that means you will beat the level on average every ~1276 attempts.
So how about if your passrate is 50% for all parts instead? 2000 attempts? No. *16384* attempts. Small changes in passrates have a very big impact.

## When the mod won't work
- The mod currently works around start positions, and you can't set any custom segments that will get saved. That means that if you use "Practice Checkpoint Permanence" for runs, many of this mod's features won't be very useful to you (yet!)
- 2.2 levels aren't guaranteed to work if they do a lot of changing directions.

## This is still a work in progress
This mod started as a small personal tool I made for myself, that I then decided I wanted to share.

That means that I can only be confident in this working for Windows, as well as the fact that there might be more kinks to work out that I just haven't been able to find.
Also I cannot upload it to Geode for now since they're actively updating and not taking new mod submissions.