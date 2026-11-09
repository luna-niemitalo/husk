#!/usr/bin/env nu
# Exports every *_hd.m2 player character model to a model bundle each,
# DB2 data (customization, NPC presets) from husk's config.

def main [
    --corpus-root: string = "/media/luna/wow_export/live"
    --listfile: string = "/home/luna/dev/husk/development/listfiles"  # husk picks the newest snapshot
    --out-dir: string = "/media/luna/work/cache/husk/hd_character_export"
    --husk-bin: string = "/home/luna/dev/husk/build/husk"
] {
    mkdir $out_dir

    let models = (
        glob $"($corpus_root)/character/**/*hd.m2"
        | sort
    )

    print $"found ($models | length) HD character models"

    for m2 in $models {
        let base = ($m2 | path basename | str replace ".m2" "")
        let out = $"($out_dir)/($base).bundle"
        print $"exporting ($base)..."

        let result = (
            ^$husk_bin export $m2 $out --listfile $listfile --listfile-root $corpus_root --anim auto
            | complete
        )

        if $result.exit_code != 0 {
            print $"  FAILED \(exit ($result.exit_code)\):"
            print ($result.stderr | lines | last 10 | str join "\n")
        }
    }

    print "done"
}
