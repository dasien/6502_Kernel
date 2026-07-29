# validate_fat16_image.cmake - check that a mkdisk-built image is a clean FAT16
# volume according to the host's own filesystem checker.
#
# The images this project ships are meant to be genuine FAT16 that any host can
# mount, and that was verified once by hand (macOS, 2026-06-14). Drawers landed two
# weeks later and their '.'/'..' entries carried a bad DIR_NTRes byte, so every image
# built after that was reported as damaged by Linux fsck.fat -- unnoticed, because
# the check was a memory rather than a test. This runs it every build instead.
#
# A dirty image is not just cosmetic: `fsck` is how the genuinely dangerous
# corruption shows up (e.g. the two FAT copies disagreeing, whose "repair" discards
# everything the machine wrote), and a checker that always complains is a checker
# nobody can read.
#
# Inputs: MKDISK (path to the tool), FSCK (path to fsck.fat), WORKDIR (scratch dir)

if(NOT MKDISK OR NOT FSCK OR NOT WORKDIR)
    message(FATAL_ERROR "MKDISK, FSCK and WORKDIR must be specified")
endif()

set(bundle "${WORKDIR}/fatchk_bundle")
set(image "${WORKDIR}/fatchk.img")
file(REMOVE_RECURSE "${bundle}")
file(REMOVE "${image}")
file(MAKE_DIRECTORY "${bundle}/GAMES")

# A drawer is essential: '.'/'..' entries only exist inside subdirectories, and that
# is where the defect lived. Two drawer files so the layout is not degenerate.
file(WRITE "${bundle}/HELLO.TXT" "hello from the host\n")
file(WRITE "${bundle}/README.TXT" "second root file\n")
file(WRITE "${bundle}/GAMES/ONE.PRG" "\x00\x08first\n")
file(WRITE "${bundle}/GAMES/TWO.PRG" "\x00\x08second\n")
file(WRITE "${bundle}/diskmap.txt"
     "HELLO.TXT\nREADME.TXT\nGAMES/ONE.PRG\nGAMES/TWO.PRG\n")

execute_process(COMMAND "${MKDISK}" create "${image}" "${bundle}/diskmap.txt"
                RESULT_VARIABLE mk_result OUTPUT_VARIABLE mk_out ERROR_VARIABLE mk_err)
if(NOT mk_result EQUAL 0)
    message(FATAL_ERROR "mkdisk failed (${mk_result}):\n${mk_out}${mk_err}")
endif()

# -n = never write; report only. Exit status is non-zero when it would have changed
# something, but check the text too: some findings are reported and auto-handled
# without affecting the status.
execute_process(COMMAND "${FSCK}" -n "${image}"
                RESULT_VARIABLE fsck_result
                OUTPUT_VARIABLE fsck_out ERROR_VARIABLE fsck_err)
set(report "${fsck_out}${fsck_err}")

if(NOT fsck_result EQUAL 0)
    message(FATAL_ERROR
        "fsck.fat reports the mkdisk image is not a clean FAT16 volume "
        "(exit ${fsck_result}):\n${report}")
endif()

if(report MATCHES "clearing|Auto-removing|auto-removing|differ|[Cc]orrupt|[Ii]nvalid|Unexpected")
    message(FATAL_ERROR
        "fsck.fat accepted the image but reported problems with it:\n${report}")
endif()

message(STATUS "fsck.fat: mkdisk image is a clean FAT16 volume")
