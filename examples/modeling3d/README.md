# Joltscript 3D animation examples

Open **3D Modeling & Animation**, add an object, and expand **Joltscript
animation**. Choose a transform channel, enter an example's path in **Animation
.jolt path**, and click **Load animation .jolt**. Play or scrub to evaluate it.

| File | Channel | Result |
|---|---|---|
| `spin.jolt` | Rotation Y (4) | Adds 90 degrees per second |
| `bounce.jolt` | Position Y (1) | Adds a looping two-unit triangular bounce |
| `clone_offset.jolt` | Position Y (1) | Staggers clones by index and lifts them over time |

For the clone example, first apply a linear, radial or grid cloner. Driver inputs
are positional: `time` in seconds, fractional `frame`, clone `index`, and the
channel's keyed `value`. The scalar output becomes the channel's final value.
Scripts are embedded in saved scenes and evaluated during preview and export.

From the repository root:

```sh
CLI=build/frontends/cli/joltfx
$CLI 3d new scene.jfx
printf '3d.script_file 0 4 0 0 examples/modeling3d/spin.jolt\nsave\n' |
  $CLI 3d edit scene.jfx spinning.jfx
$CLI 3d render spinning.jfx spinning.png 1.0
```

See [the 3D guide](../../docs/modeling3d.md) for supported operators, generator
controls, cloner layouts, camera navigation and limits.
