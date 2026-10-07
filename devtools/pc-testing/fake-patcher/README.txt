Fake PortMaster patcher for PC testing of a launcher's first-run setup.
utils/patcher.txt mimics PortMaster-GUI's patcher: runs $PATCHER_FILE through a pipe (like
io.popen), prints its stdout with a [GUI] prefix and always ends with "Patching completed
successfully!" (the real one ignores the exit code too). Point controlfolder at this folder.
test_setup_ittledew.sh: example harness (Ittle Dew) that extracts the launcher's setup block,
stubs pm_message/sleep, and runs launch 1 + launch 2 for several gamedata cases.
