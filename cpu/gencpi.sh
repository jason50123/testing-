#!/bin/bash
cd $(dirname $(realpath $0))

cpis="$(python3 generator/generate.py | tr '\n' ' ' | tr --delete ' ' | sed 's/cpi.find/\ncpi.find/g')"
defs="$(cat def.hh | tr -s '\n')"

# the enum parser and cpi table converter
{ echo "$defs"; echo "$cpis"; } | awk '
BEGIN {
    # use dict ENUMS[ENUM_IDX] to save the NAMESPACE and FUNCTION enum members
    ENUM_IDX = 0
    MEMB_IDX = 0

    print "  // clang-format off"
    print "  { // used for folding this section"
}

# end dict before saving enum member of this line
(ENUM_IDX % 2) != 0 && /} (NAMESPACE|FUNCTION);/ {
    ENUM_IDX += 1
}

# save string (enum member) into dict (value should match MEMB_IDX)
(ENUM_IDX % 2) != 0 && ($1 !~ /^\/\//) {
    name = $1
    gsub(/,.*$/, "", name)
    ENUMS[ENUM_IDX][MEMB_IDX++] = name
}

# start dict after skipping parsing this line
ENUM_IDX < 3 && /typedef enum/ {
    ENUM_IDX += 1
    MEMB_IDX = 0
}

# convert the CPI tables
ENUM_IDX > 3 && /cpi.find/ {
    # extract NS and FCT: groups[1] for NS, groups[2] for FCT
    match($0, /cpi\.find\(([0-9]+))->second\.insert\({([0-9]+),.*/, groups)
    ns = ENUMS[1][groups[1]]
    fct = ENUMS[3][groups [2]]

    # replace the original format
    sub(/find\([0-9]+\)/,"find(" ns ")" ,$0)
    sub(/[0-9]+,InstStat/, fct ",InstStat" ,$0)
    printf "  %s\n", $0
}

END {
    print ""
    print "// check values defines in functions.py match those defined in def.hh"
    print "#define ERR_MSG \"Unexpected NAMESPACE ID\""
    for (i in ENUMS[1]) {
        printf "  static_assert(NAMESPACE::%s == %d, ERR_MSG);\n", ENUMS[1][i], i
    }
    print "#undef ERR_MSG"

    print "#define ERR_MSG \"Unexpected FUNCTION ID\""
    for (i in ENUMS[3]) {
        printf "  static_assert(FUNCTION::%s == %d, ERR_MSG);\n", ENUMS[3][i], i
    }
    print "#undef ERR_MSG"
    print "  } // used for folding this section"
    print "  // clang-format on"
}
'

echo "$result"