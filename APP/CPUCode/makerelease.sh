#!/bin/bash
#---------------------------------------------------------------------------------------------------
# AppLibrary release script 
#---------------------------------------------------------------------------------------------------
set -u
set -e


#---------------------------------------------------------------------------------------------------
# Project configuration
#---------------------------------------------------------------------------------------------------

# Short name of the project
# This will be the name of the release and of the binary file. It should only contain alphanumeric
# and underscore characters
project_short_name=

# Long name of the project
# This is used in the header comments added to source files
project_long_name=""

# Author of the project
# This is used in the header comments added to source files
author=""

# This script copies all C/C++ files in the release. Other required files should be located in the
# extra/ directory.
# The Makefile.release file should be modified where needed so that the release can be build.

#---------------------------------------------------------------------------------------------------
# Internal configuration
#---------------------------------------------------------------------------------------------------
function setConfig {
    # Directory of this script
    project_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

    # Directory that contains host code
    cpu_dir="$project_dir/CPUCode"

    # Release name
    release_name="${project_short_name}_V${version/\./_}"

    # The release will be created in this directory
    release_dir="$cpu_dir/$release_name"

    # Directory that contains HW maxfiles
    hw_max_dir="$project_dir/Profiles/Hardware/maxfiles"

    # Path to the maxHeader.py script
    maxhdr="maxHeader.py"

    # Name of the release archive
    release_file="$release_name.tgz"

    # Name of the release checksum file
    checksum_file="$release_name.md5sum"

    # Release date
    date="$(date "+%d %b %Y")"

    # Path to sliccompile
    sliccompile=$MAXCOMPILERDIR/bin/sliccompile

    # Path to the SLiC library
    libslic=$MAXCOMPILERDIR/lib/libslic.a

    # Name of the static library providing the interface to run the DFE
    dfe_lib=libdfe.a

    # Space-separated list of maxfiles
    maxfiles=( $(sed -ne 's/<maxFile buildName=\"\([A-Za-z0-9_\-]*\)\".*/\1.max/p' "$project_dir/RunRules/Hardware/RunRules.settings") )

    # Maximum size in bytes for the release
    max_size_bytes=16777216
}

#---------------------------------------------------------------------------------------------------
# Build a release
#---------------------------------------------------------------------------------------------------
function doRelease {

    # Check project configuration
    if [ -z "$project_short_name" ]; then
        echo "error: Project configuration variable 'project_short_name' is empty.">&2
        exit 1
    fi

    if [ -z "$project_long_name" ]; then
        echo "error: Project configuration variable 'project_long_name' is empty.">&2
        exit 1
    fi

    if [ -z "$author" ]; then
        echo "error: Project configuration variable 'author' is empty.">&2
        exit 1
    fi

    if [ -z "$project_short_name" -o -n "$(echo "$project_short_name" | tr -d '[:alnum:]_')" ]; then
        echo "error: 'project_short_name' is invalid. It should only contain alphanumeric and underscore characters.">&2
        exit 1
    fi

    if [ -z "$version" -o -n "$(echo "$version" | sed -r -e 's/^[0-9]+\.[0-9]+$//')" ]; then
        echo "error: The release version must have the format 'major.minor' (for example: -r 1.0).">&2
        exit 1
    fi

    if [ -e "$release_file" ]; then
        echo "error: File already exists: '$release_file'">&2
        exit 1
    fi

    if [ -e "$checksum_file" ]; then
        echo "error: File already exists: '$checksum_file'">&2
        exit 1
    fi

    if [ -e "$release_dir" ]; then
        echo "error: Could not create the release directory '$release_dir'. File already exists.">&2
        exit 1
    fi

    # Create the release directory
    mkdir "$release_dir"

    # Test for required files
    if [ -f "$cpu_dir/main.c" ]; then
        is_c=1
    elif [ -f "$cpu_dir/main.cpp" ]; then
        is_c=0
    else
        echo "error: Missing 'main.c' or 'main.cpp' file." >&2
        exit 1
    fi

    required_files=(
        "$cpu_dir/README.txt" \
    )
    for f in "${required_files[@]}" ; do
        if [ ! -f "$f" ]; then
            echo "error: Required file '$f' is missing." >&2
            exit 1
        fi
    done

    # Copy and comment host code files
    echo "Copying source files..."
    OLDIFS=$IFS
    IFS=$'\n'
    sources=(
        $(find "$cpu_dir" -name "*.h") \
        $(find "$cpu_dir" -name "*.c") \
        $(find "$cpu_dir" -name "*.cpp") \
    )
    IFS=$OLDIFS
    for s in "${sources[@]}" ; do
        file=$(basename "$s")
        cp "$cpu_dir/$file" "$release_dir/$file"
    done

    cp "$cpu_dir/README.txt" "$release_dir/README.txt"

    # Compile maxfiles
    echo "Compiling maxfiles..."
    for f in "${maxfiles[@]}"; do
        cp "$hw_max_dir/${f%.max}.h" "$release_dir/"
        $sliccompile "$hw_max_dir/$f" "$release_dir/${f%.max}.o"
    done

    # Create a static library
    echo "Creating the DFE static library..."
    cd "$release_dir"
    ar -x "$libslic"
    ar -cq "$dfe_lib" *.o
    rm *.o
    cd - > /dev/null

    # Copy the Makefile
    cp "$cpu_dir/Makefile.release" "$release_dir/Makefile"

    # Extra files
    if [ -d "$cpu_dir/extra/" ]; then
        echo "Copying additional files..."
        rsync -r --exclude='.*' "$cpu_dir/extra/" "$release_dir/"
    fi

    mkdir "$release_dir/includes/"
    cp $MAXCOMPILERDIR/include/slic/*.h "$release_dir/includes/"

    # Tar the release directory
    echo "Creating the release file..."
    tar czf "$release_file" -C "$release_dir/.." "$release_name"
    md5sum "$release_file" > "$checksum_file"
    rm -rf "$release_dir"
    release_size_human=$(ls -lh "$release_file" | awk '{ print $5}')
    release_size_bytes=$(ls -l --block-size=1 "$release_file" | awk '{ print $5}')
    release_checksum=$(md5sum "$release_file" | cut -f1 -d" ")

    echo ""
    echo "Release file: $release_file"
    echo "Size: $release_size_human"
    echo "Checksum: $release_checksum"

    if [ $release_size_bytes -gt $max_size_bytes ]; then
        echo "error: The release file is too large.". 2>&1
        exit 1
    fi
}


#---------------------------------------------------------------------------------------------------
# Test a release
#---------------------------------------------------------------------------------------------------
function doTest {
    if [ ! -f "$test_archive" ]; then
        echo "error: '$test_archive' does not exist or is not a file". 2>&1
        exit 1
    fi

    test_dir=${test_archive%.tgz}
    if [ -e "$test_dir" ]; then
        echo "error: '$test_dir' already exists." 2>&1
        exit 1
    fi

    echo "Uncompressing the release file..."
    tar xzf "$test_archive"
    echo "Building the application..."
    set +e
    make -C "$test_dir"
    if [ $? -ne 0 ] ; then
        echo "error: Failed to build the application.". 2>&1
        echo "error: Please check that your configuration and Makefile.release are correct". 2>&1
        rm -rf "$test_dir"
        exit 1
    fi
    set -e
    rm -rf "$test_dir"
    echo SUCCESS
}


#---------------------------------------------------------------------------------------------------
# Parameters parsing
#---------------------------------------------------------------------------------------------------

function showUsage {
    echo "usage: $(basename $0) [-r VERSION] [-t FILE]"
}

function showHelp {
    echo "usage: $(basename $0) [-r VERSION] [-t FILE]"
    echo ""
    echo "  -h               Print help and exit"
    echo "  -r VERSION       Build a release"
    echo "  -t FILE          Test a release"
}

do_test=0
do_release=0
version=""
test_archive=""

while getopts "hr:t:" opt; do
    case $opt in
        h)
            showHelp
            exit 0
            ;;
        r)
            version=$OPTARG
            do_release=1
            ;;
        t)
            test_archive=$OPTARG
            do_test=1
            ;;
        \?)
            showUsage
            exit 1
            ;;
    esac
done

if [ $do_release -eq 1 ]; then
    setConfig
    doRelease
elif [ $do_test -eq 1 ]; then
    doTest
else
    showHelp
    exit 0
fi 
