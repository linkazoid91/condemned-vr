# Strict warning policy inherited by project-owned targets.
add_library(fearvr-warnings INTERFACE)

target_compile_options(fearvr-warnings INTERFACE
  /W4
  /permissive-
  /utf-8
)

option(CONDEMNEDVR_WARNINGS_AS_ERRORS
  "Treat warnings as errors for project targets" OFF)
if(CONDEMNEDVR_WARNINGS_AS_ERRORS)
  target_compile_options(fearvr-warnings INTERFACE /WX)
endif()
