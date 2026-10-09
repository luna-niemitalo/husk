#!/usr/bin/env nu
# WIP: ADT tiles -> one terrain bundle per tile plus a shared directory of
# canon model bundles for every M2 they reference (placed doodads and
# ground-effect detail doodads), linked by relative uri. See
# TODO/WORLD/ADT_EXPORT_FINDINGS.md.
#
#   <out>/tiles/<tile>.bundle/manifest.json
#   <out>/models/<fdid>.canon.bundle/manifest.json
#
# husk export-terrain only references models; producing them is husk
# export --bundle-only's job, one process per model, so the loop lives here.

def main [
    out: string            # scene directory to write
    ...tiles: string       # root .adt files, e.g. .../azeroth/azeroth_32_49.adt
    --corpus-root: string = "/media/luna/data/wow_export"
    --listfile: string = "/media/luna/userdata/Downloads/community-listfile.csv"
    --db2-dir: string = "/media/luna/data/wow_export/dbfilesclient"
    --dbd-dir: string = "/home/luna/dev/husk/reference/WoWDBDefs"
    --husk-bin: string = "/home/luna/dev/husk/build/husk"
] {
    let common = [--listfile $listfile --listfile-root $corpus_root --db2-dir $db2_dir --dbd-dir $dbd_dir]
    let models_dir = $"($out)/models"
    mkdir $models_dir $"($out)/tiles"

    let models = (
        $tiles
        | each {|t| ^$husk_bin export-terrain $t $"($out)/tiles/unused" ...$common --list-models | lines }
        | flatten
        | uniq
        | split column "\t" fdid path
    )
    print $"($models | length) distinct referenced models across ($tiles | length) tiles"

    let failed = (
        $models | each {|m|
            let bundle = $"($models_dir)/($m.fdid).canon.bundle"
            if ($bundle | path exists) { return null }
            if ($m.path | is-empty) { return {fdid: $m.fdid, reason: "not in listfile"} }
            let src = $"($corpus_root)/($m.path)"
            if not ($src | path exists) { return {fdid: $m.fdid, reason: $"not extracted: ($m.path)"} }
            let r = (^$husk_bin export $src $bundle --bundle-only --listfile $listfile --listfile-root $corpus_root | complete)
            if $r.exit_code != 0 or not ($bundle | path exists) {
                {fdid: $m.fdid, path: $m.path, reason: ($r.stderr | lines | last 1 | str join)}
            }
        }
    )
    if ($failed | is-not-empty) {
        print $"($failed | length) models not exported \(references keep identity only\):"
        print ($failed | table)
    }

    for t in $tiles {
        let name = ($t | path basename | str replace ".adt" "")
        ^$husk_bin export-terrain $t $"($out)/tiles/($name).bundle" ...$common --models-dir $models_dir
    }
}
