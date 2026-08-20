#!/usr/bin/env nu
# Watches a vkd3d-proton VKD3D_SHADER_DUMP_PATH directory for newly captured
# shaders and fires a popup notification per new shader hash, so a capture
# session can be correlated against "where in-game was I when this landed."
# Dedupes by watching only *.spv creation, not *.dxbc -- both land together
# for a real shader pair (see SHADER_SCAN_FINDINGS.md), so one notification
# per pair is enough.

def main [
  --dump-dir: string = "/media/luna/work/cache/wow_shader_dump"
  --log: string = "/home/luna/dev/husk/references/wow_shaders/capture_log.csv"
  --timeout: int = 8000  # ms; critical urgency persists forever without an explicit expire-time
] {
  if not ($log | path exists) {
    "timestamp,hash\n" | save $log
  }

  print $"Watching ($dump_dir) for new shader captures -- logging to ($log)"
  print "Ctrl-C to stop."

  ^inotifywait -m -e create --format "%f" $dump_dir
  | lines
  | where {|f| $f | str ends-with ".spv" }
  | each {|filename|
      let hash = ($filename | str replace ".spv" "")
      let ts = (date now | format date "%Y-%m-%d %H:%M:%S")
      # Plasma ignores --expire-time on critical-urgency notifications by
      # policy (verified interactively) -- self-dismiss via D-Bus instead.
      let id = (
        notify-send -p --urgency critical --app-name "wow-shader-watch" "New WoW shader captured" $"($hash)\ncaptured ($ts) -- note where you are in-game now"
        | str trim
      )
      let close_delay_s = ($timeout / 1000)
      ^bash -c $"sleep ($close_delay_s); gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications --method org.freedesktop.Notifications.CloseNotification ($id) >/dev/null 2>&1 &"
      $"($ts),($hash)\n" | save --append $log
      print $"[($ts)] new shader: ($hash)"
    }
}
