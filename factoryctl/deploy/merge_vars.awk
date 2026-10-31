# Prints the first file (deploy/config_vars.yaml) over the second (server/configs/config_vars.yaml):
# the second's lines whose keys the first has are dropped, the first is appended. Both are flat
# `key: value` files. The image keeps the result as its config_vars, so that the one
# --config_vars_override userver takes is left for the host's own file.
#   awk -f merge_vars.awk deploy/config_vars.yaml server/configs/config_vars.yaml
function key(line) {
    return match(line, /^[A-Za-z0-9_-]+:/) ? substr(line, 1, RLENGTH) : ""
}
NR == FNR {
    over[++n] = $0
    if (key($0) != "") ours[key($0)] = 1
    next
}
key($0) == "" || !(key($0) in ours)
END {
    print ""
    for (i = 1; i <= n; i++) print over[i]
}
