#!/usr/bin/env nu
# Batch-exports every real *_hd.m2 player character model to .glb.
# husk export is single-file only (no batch mode yet -- see
# TODO/CLEANUP_TODO.md), so this is the loop until that lands.

def main [
    --corpus-root: string = "/media/luna/data/wow_export"
    --listfile: string = "/media/luna/userdata/Downloads/community-listfile.csv"
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
        let out = $"($out_dir)/($base).glb"
        print $"exporting ($base)..."

        # No --lod: passing it (even "0", the default) routes 'auto' through
        # resolveAutoSkinPaths, which has no same-basename-numbered-scan
        # fallback -- unlike the no-lod resolveSkin path. See
        # TODO/CLEANUP_TODO.md #4.
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
