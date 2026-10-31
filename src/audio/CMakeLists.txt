#  The sound effects and the music, where SDL3 was found.
if(SDL3_FOUND)
    target_sources(arcology PRIVATE sound.c music.c)
endif()
