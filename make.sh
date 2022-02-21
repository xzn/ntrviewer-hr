export CPATH="`realpath include`"
export LIBRARY_PATH="`realpath lib`"
make -j$(nproc) $@
