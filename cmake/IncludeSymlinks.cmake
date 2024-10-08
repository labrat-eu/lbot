cmake_minimum_required(VERSION 3.22.0)

function(prj_add_include_symlink)
  set(options)
  set(oneValueArgs TARGET NAME NAMESPACE)
  set(multiValueArgs)
  cmake_parse_arguments(INT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Create a symbolic link from the include directory path to the passed target.
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/${INT_NAMESPACE})
  file(CREATE_LINK ${INT_TARGET} ${CMAKE_BINARY_DIR}/include/${INT_NAMESPACE}/${INT_NAME} SYMBOLIC)
endfunction(prj_add_include_symlink)
